#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef DYN_SYMPHONY
#  define DYN_NEXT_PC(pc, size) ((pc) + 4u)
#else
#  define DYN_NEXT_PC(pc, size) ((pc) + (size))
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define DYN_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define DYN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define DYN_LIKELY(x)   (x)
#  define DYN_UNLIKELY(x) (x)
#endif

/*
 * Optimized Dynphony interpreter.
 *
 * Main differences from the original implementation:
 *   - dedicated byte/BE16/BE32 memory helpers
 *   - lazy predecode cache indexed by guest PC
 *   - specialized micro-ops (RR vs RI, load/store width, branch condition, etc.)
 *   - direct-threaded dispatch (computed goto) on GCC/Clang
 *   - portable switch-dispatch fallback for MSVC
 *   - precise decode-cache invalidation for self-modifying stores
 *
 * Python objects remain only at the boundary and for guest I/O instructions.
 */

typedef enum {
    U_INVALID = 0,
    U_NOP,
    U_IN,
    U_OUT_R,
    U_OUT_I,
    U_KEY,
    U_SCREEN_R,
    U_SCREEN_I,
    U_TIME_LO,
    U_TIME_HI,
    U_GETPC,

    U_NAND_RR, U_OR_RR, U_AND_RR, U_NOR_RR, U_ADD_RR, U_SUB_RR,
    U_XOR_RR, U_SHL_RR, U_SHR_RR, U_SAR_RR, U_CMP_RR,
    U_NAND_RI, U_OR_RI, U_AND_RI, U_NOR_RI, U_ADD_RI, U_SUB_RI,
    U_XOR_RI, U_SHL_RI, U_SHR_RI, U_SAR_RI, U_CMP_RI,

    U_JEQ_R, U_JNE_R, U_JLTU_R, U_JGEU_R, U_JLEU_R, U_JGTU_R,
    U_JLTS_R, U_JGES_R, U_JLES_R, U_JGTS_R, U_JMP_R,
    U_JEQ_I, U_JNE_I, U_JLTU_I, U_JGEU_I, U_JLEU_I, U_JGTU_I,
    U_JLTS_I, U_JGES_I, U_JLES_I, U_JGTS_I, U_JMP_I,

    U_LOAD8_R, U_LOAD16_R, U_LOAD32_R, U_PLOAD32_R,
    U_STORE8_R, U_STORE16_R, U_STORE32_R, U_PSTORE32_R,
    U_LOAD8_I, U_LOAD16_I, U_LOAD32_I, U_PLOAD32_I,
    U_STORE8_I, U_STORE16_I, U_STORE32_I, U_PSTORE32_I,

    U_COUNT
} UOp;

typedef struct {
    uint8_t valid;
    uint8_t uop;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t opcode;
    uint16_t _pad;
    uint32_t imm;
    uint32_t next_pc;
    uint32_t pc_tag;
} Decoded;

typedef struct {
    PyObject *machine;               /* borrowed */
    PyObject *regs_obj;              /* owned */
    PyObject *inputs;                /* owned */
    PyObject *keyboard_inputs;       /* owned */
    PyObject *outputs;               /* owned */
    PyObject *screen_updates;        /* owned */
    PyObject *comparison_obj;        /* owned */
    PyObject *persistent_obj;        /* owned */

    uint8_t *memory;
    uint8_t *persistent;
    uint32_t regs[16];
    uint32_t mask;
    uint32_t persistent_mask;
    uint32_t pc;
    uint64_t steps;
    uint64_t time_value;
    int comparison_valid;
    uint32_t comparison_a;
    uint32_t comparison_b;
    int has_persistent;

    Decoded *decode;
    size_t decode_count;
} State;

static inline uint8_t mem8(const uint8_t *m, uint32_t mask, uint32_t a) {
    return m[a & mask];
}

static inline uint16_t mem16be(const uint8_t *m, uint32_t mask, uint32_t a) {
    uint32_t p = a & mask;
    if (DYN_LIKELY(p <= mask - 1u)) {
        return (uint16_t)(((uint16_t)m[p] << 8) | m[p + 1u]);
    }
    return (uint16_t)(((uint16_t)m[p] << 8) | m[(a + 1u) & mask]);
}

static inline uint32_t mem32be(const uint8_t *m, uint32_t mask, uint32_t a) {
    uint32_t p = a & mask;
    if (DYN_LIKELY(p <= mask - 3u)) {
        return ((uint32_t)m[p] << 24) |
               ((uint32_t)m[p + 1u] << 16) |
               ((uint32_t)m[p + 2u] << 8) |
               (uint32_t)m[p + 3u];
    }
    return ((uint32_t)m[p] << 24) |
           ((uint32_t)m[(a + 1u) & mask] << 16) |
           ((uint32_t)m[(a + 2u) & mask] << 8) |
           (uint32_t)m[(a + 3u) & mask];
}

static inline void store8(uint8_t *m, uint32_t mask, uint32_t a, uint32_t v) {
    m[a & mask] = (uint8_t)v;
}

static inline void store16be(uint8_t *m, uint32_t mask, uint32_t a, uint32_t v) {
    uint32_t p = a & mask;
    if (DYN_LIKELY(p <= mask - 1u)) {
        m[p] = (uint8_t)(v >> 8);
        m[p + 1u] = (uint8_t)v;
    } else {
        m[p] = (uint8_t)(v >> 8);
        m[(a + 1u) & mask] = (uint8_t)v;
    }
}

static inline void store32be(uint8_t *m, uint32_t mask, uint32_t a, uint32_t v) {
    uint32_t p = a & mask;
    if (DYN_LIKELY(p <= mask - 3u)) {
        m[p] = (uint8_t)(v >> 24);
        m[p + 1u] = (uint8_t)(v >> 16);
        m[p + 2u] = (uint8_t)(v >> 8);
        m[p + 3u] = (uint8_t)v;
    } else {
        m[p] = (uint8_t)(v >> 24);
        m[(a + 1u) & mask] = (uint8_t)(v >> 16);
        m[(a + 2u) & mask] = (uint8_t)(v >> 8);
        m[(a + 3u) & mask] = (uint8_t)v;
    }
}

static PyObject *get_attr(PyObject *object, const char *name) {
    return PyObject_GetAttrString(object, name);
}

static int load_state(State *s, PyObject *machine) {
    PyObject *obj = NULL;
    Py_ssize_t i;
    Py_ssize_t memory_size;
    memset(s, 0, sizeof(*s));
    s->machine = machine;

    obj = get_attr(machine, "memory");
    if (!obj || !PyByteArray_Check(obj)) {
        Py_XDECREF(obj);
        PyErr_SetString(PyExc_TypeError, "machine.memory must be a bytearray");
        return -1;
    }
    memory_size = PyByteArray_GET_SIZE(obj);
    if (memory_size <= 0) {
        Py_DECREF(obj);
        PyErr_SetString(PyExc_ValueError, "machine.memory must not be empty");
        return -1;
    }
    s->memory = (uint8_t *)PyByteArray_AS_STRING(obj);
    Py_DECREF(obj);

#define LOAD_OWNED(field, name) do { \
    s->field = get_attr(machine, name); \
    if (!s->field) return -1; \
} while (0)
    LOAD_OWNED(regs_obj, "regs");
    LOAD_OWNED(inputs, "inputs");
    LOAD_OWNED(keyboard_inputs, "keyboard_inputs");
    LOAD_OWNED(outputs, "outputs");
    LOAD_OWNED(screen_updates, "screen_updates");
    LOAD_OWNED(comparison_obj, "comparison");
    LOAD_OWNED(persistent_obj, "persistent");
#undef LOAD_OWNED

    if (!PyList_Check(s->regs_obj) || PyList_GET_SIZE(s->regs_obj) != 16) {
        PyErr_SetString(PyExc_TypeError, "machine.regs must be a 16-item list");
        return -1;
    }
    for (i = 0; i < 16; ++i) {
        s->regs[i] = (uint32_t)PyLong_AsUnsignedLongMask(PyList_GET_ITEM(s->regs_obj, i));
        if (PyErr_Occurred()) return -1;
    }

#define LOAD_U32(name, target) do { \
    obj = get_attr(machine, name); \
    if (!obj) return -1; \
    target = (uint32_t)PyLong_AsUnsignedLongMask(obj); \
    Py_DECREF(obj); \
    if (PyErr_Occurred()) return -1; \
} while (0)
    LOAD_U32("mask", s->mask);
    LOAD_U32("pc", s->pc);
#undef LOAD_U32

    /* The implementation relies on the original emulator's mask semantics. */
    if ((uint64_t)s->mask + 1u > (uint64_t)memory_size) {
        PyErr_SetString(PyExc_ValueError, "machine.mask addresses beyond machine.memory");
        return -1;
    }

    obj = get_attr(machine, "steps");
    if (!obj) return -1;
    s->steps = PyLong_AsUnsignedLongLong(obj);
    Py_DECREF(obj);
    if (PyErr_Occurred()) return -1;

    obj = get_attr(machine, "time_value");
    if (!obj) return -1;
    s->time_value = PyLong_AsUnsignedLongLongMask(obj);
    Py_DECREF(obj);
    if (PyErr_Occurred()) return -1;

    if (s->comparison_obj != Py_None) {
        if (!PyTuple_Check(s->comparison_obj) || PyTuple_GET_SIZE(s->comparison_obj) != 2) {
            PyErr_SetString(PyExc_TypeError, "invalid machine comparison state");
            return -1;
        }
        s->comparison_valid = 1;
        s->comparison_a = (uint32_t)PyLong_AsUnsignedLongMask(PyTuple_GET_ITEM(s->comparison_obj, 0));
        s->comparison_b = (uint32_t)PyLong_AsUnsignedLongMask(PyTuple_GET_ITEM(s->comparison_obj, 1));
        if (PyErr_Occurred()) return -1;
    }

    if (!PyByteArray_Check(s->persistent_obj)) {
        PyErr_SetString(PyExc_TypeError, "machine.persistent must be a bytearray");
        return -1;
    }
    if (PyByteArray_GET_SIZE(s->persistent_obj) != 0) {
        s->has_persistent = 1;
        s->persistent = (uint8_t *)PyByteArray_AS_STRING(s->persistent_obj);
        obj = get_attr(machine, "persistent_mask");
        if (!obj) return -1;
        s->persistent_mask = (uint32_t)PyLong_AsUnsignedLongMask(obj);
        Py_DECREF(obj);
        if (PyErr_Occurred()) return -1;
        if ((uint64_t)s->persistent_mask + 1u > (uint64_t)PyByteArray_GET_SIZE(s->persistent_obj)) {
            PyErr_SetString(PyExc_ValueError, "machine.persistent_mask addresses beyond persistent memory");
            return -1;
        }
    }

    s->decode_count = (size_t)s->mask + 1u;
    if (s->decode_count > SIZE_MAX / sizeof(Decoded)) {
        PyErr_NoMemory();
        return -1;
    }
    s->decode = (Decoded *)PyMem_Calloc(s->decode_count, sizeof(Decoded));
    if (!s->decode) {
        PyErr_NoMemory();
        return -1;
    }
    return 0;
}

static void release_state(State *s) {
    PyMem_Free(s->decode);
    s->decode = NULL;
    Py_XDECREF(s->regs_obj);
    Py_XDECREF(s->inputs);
    Py_XDECREF(s->keyboard_inputs);
    Py_XDECREF(s->outputs);
    Py_XDECREF(s->screen_updates);
    Py_XDECREF(s->comparison_obj);
    Py_XDECREF(s->persistent_obj);
}

static int set_attr_u64(PyObject *object, const char *name, uint64_t value) {
    PyObject *number = PyLong_FromUnsignedLongLong(value);
    int result;
    if (!number) return -1;
    result = PyObject_SetAttrString(object, name, number);
    Py_DECREF(number);
    return result;
}

static int sync_state(State *s) {
    PyObject *value = NULL;
    PyObject *comparison = NULL;
    int i;
    for (i = 0; i < 16; ++i) {
        value = PyLong_FromUnsignedLong(s->regs[i]);
        if (!value) return -1;
        if (PyList_SetItem(s->regs_obj, i, value) < 0) return -1; /* steals value */
    }
    if (set_attr_u64(s->machine, "pc", s->pc) < 0 ||
        set_attr_u64(s->machine, "steps", s->steps) < 0)
        return -1;

    if (s->comparison_valid) {
        comparison = Py_BuildValue("(II)", s->comparison_a, s->comparison_b);
        if (!comparison) return -1;
    } else {
        comparison = Py_None;
        Py_INCREF(comparison);
    }
    if (PyObject_SetAttrString(s->machine, "comparison", comparison) < 0) {
        Py_DECREF(comparison);
        return -1;
    }
    Py_DECREF(comparison);
    return 0;
}

static int append_u32(PyObject *list, uint32_t value) {
    PyObject *number = PyLong_FromUnsignedLong(value);
    int result;
    if (!number) return -1;
    result = PyList_Append(list, number);
    Py_DECREF(number);
    return result;
}

static int queue_pop(PyObject *queue, uint32_t *result) {
    PyObject *value;
    int truth = PyObject_IsTrue(queue);
    if (truth < 0) return -1;
    if (!truth) {
        *result = 0;
        return 0;
    }
    value = PyObject_CallMethod(queue, "popleft", NULL);
    if (!value) return -1;
    *result = (uint32_t)PyLong_AsUnsignedLongMask(value);
    Py_DECREF(value);
    return PyErr_Occurred() ? -1 : 0;
}

static int append_screen(PyObject *list, uint32_t setting, uint32_t value) {
    PyObject *pair = Py_BuildValue("(II)", setting, value);
    int result;
    if (!pair) return -1;
    result = PyList_Append(list, pair);
    Py_DECREF(pair);
    return result;
}

static inline void invalidate_decode(State *s, uint32_t address, unsigned size) {
    /* Any <=4-byte instruction beginning up to three bytes before the write can overlap it. */
    int delta;
    int end = (int)size - 1;
    for (delta = -3; delta <= end; ++delta) {
        uint32_t p = (address + (uint32_t)delta) & s->mask;
        s->decode[p].valid = 0;
    }
}

static int decode_instruction(State *s, uint32_t pc, Decoded *d) {
    const uint8_t *m = s->memory;
    const uint32_t mask = s->mask;
    uint8_t op = mem8(m, mask, pc);
    uint8_t x, code, base;
    int immediate;

    memset(d, 0, sizeof(*d));
    d->opcode = op;
    d->uop = U_INVALID;
    d->pc_tag = pc;

    if (op == 0x00) {
        d->uop = U_NOP;
        d->next_pc = DYN_NEXT_PC(pc, 1u);
    } else if (op == 0x08) {
        /* The run loop treats a stationary PC as a halted machine. */
        d->uop = U_NOP;
        d->next_pc = pc;
    } else if (op == 0x01) {
        d->uop = U_IN;
        d->a = mem8(m, mask, pc + 1u) >> 4;
        d->next_pc = DYN_NEXT_PC(pc, 2u);
    } else if (op == 0x02) {
        d->uop = U_OUT_R;
        d->a = mem8(m, mask, pc + 2u) & 15u;
        d->next_pc = DYN_NEXT_PC(pc, 3u);
    } else if (op == 0x12) {
        d->uop = U_OUT_I;
        d->imm = mem16be(m, mask, pc + 2u);
        d->next_pc = DYN_NEXT_PC(pc, 4u);
    } else if (op == 0x03) {
        d->uop = U_KEY;
        d->a = mem8(m, mask, pc + 1u) >> 4;
        d->next_pc = DYN_NEXT_PC(pc, 2u);
    } else if (op == 0x04) {
        d->uop = U_SCREEN_R;
        d->a = mem8(m, mask, pc + 1u) & 15u;
        d->c = mem8(m, mask, pc + 2u) & 15u;
        d->next_pc = DYN_NEXT_PC(pc, 3u);
    } else if (op == 0x14) {
        d->uop = U_SCREEN_I;
        d->a = mem8(m, mask, pc + 1u) & 15u;
        d->imm = mem16be(m, mask, pc + 2u);
        d->next_pc = DYN_NEXT_PC(pc, 4u);
    } else if (op == 0x05) {
        d->uop = U_TIME_LO;
        d->a = mem8(m, mask, pc + 1u) >> 4;
        d->next_pc = DYN_NEXT_PC(pc, 2u);
    } else if (op == 0x06) {
        d->uop = U_TIME_HI;
        d->a = mem8(m, mask, pc + 1u) >> 4;
        d->next_pc = DYN_NEXT_PC(pc, 2u);
    } else if (op == 0x07) {
        d->uop = U_GETPC;
        d->a = mem8(m, mask, pc + 1u) >> 4;
        d->next_pc = DYN_NEXT_PC(pc, 2u);
    } else if (op >= 0x20 && op <= 0x3a && (op & 15u) <= 10u) {
        x = mem8(m, mask, pc + 1u);
        d->a = x >> 4;       /* dst */
        d->b = x & 15u;      /* lhs */
        code = op & 15u;
        immediate = (op & 16u) != 0;
        if (immediate) {
            d->imm = mem16be(m, mask, pc + 2u);
            d->uop = (uint8_t)(U_NAND_RI + code);
            d->next_pc = DYN_NEXT_PC(pc, 4u);
        } else {
            d->c = mem8(m, mask, pc + 2u) & 15u;
            d->uop = (uint8_t)(U_NAND_RR + code);
            d->next_pc = DYN_NEXT_PC(pc, 3u);
        }
    } else if (op >= 0x40 && op <= 0x5f) {
        immediate = (op & 16u) != 0;
        base = op & 0xefu;
        if (immediate) {
            d->imm = mem16be(m, mask, pc + 2u);
            d->next_pc = DYN_NEXT_PC(pc, 4u);
        } else {
            d->c = mem8(m, mask, pc + 2u) & 15u;
            d->next_pc = DYN_NEXT_PC(pc, 3u);
        }
        switch (base) {
            case 0x41: d->uop = immediate ? U_JEQ_I  : U_JEQ_R;  break;
            case 0x49: d->uop = immediate ? U_JNE_I  : U_JNE_R;  break;
            case 0x42: d->uop = immediate ? U_JLTU_I : U_JLTU_R; break;
            case 0x4a: d->uop = immediate ? U_JGEU_I : U_JGEU_R; break;
            case 0x43: d->uop = immediate ? U_JLEU_I : U_JLEU_R; break;
            case 0x4b: d->uop = immediate ? U_JGTU_I : U_JGTU_R; break;
            case 0x44: d->uop = immediate ? U_JLTS_I : U_JLTS_R; break;
            case 0x4c: d->uop = immediate ? U_JGES_I : U_JGES_R; break;
            case 0x45: d->uop = immediate ? U_JLES_I : U_JLES_R; break;
            case 0x4d: d->uop = immediate ? U_JGTS_I : U_JGTS_R; break;
            case 0x48: d->uop = immediate ? U_JMP_I  : U_JMP_R;  break;
            default: break;
        }
    } else if (op >= 0x60 && op <= 0x77) {
        immediate = (op & 16u) != 0;
        code = op & 7u;
        x = mem8(m, mask, pc + 1u);
        d->a = (code < 4u) ? (x >> 4) : (x & 15u); /* load dst / store src */
        if (immediate) {
            d->imm = mem16be(m, mask, pc + 2u);
            d->uop = (uint8_t)(U_LOAD8_I + code);
            d->next_pc = DYN_NEXT_PC(pc, 4u);
        } else {
            d->c = mem8(m, mask, pc + 2u) & 15u;
            d->uop = (uint8_t)(U_LOAD8_R + code);
            d->next_pc = DYN_NEXT_PC(pc, 3u);
        }
    }

    d->valid = 1;
    return 0;
}

static inline Decoded *decoded_at(State *s, uint32_t pc) {
    Decoded *d = &s->decode[pc & s->mask];
    if (DYN_UNLIKELY(!d->valid || d->pc_tag != pc))
        decode_instruction(s, pc, d);
    return d;
}

static inline uint32_t sar32(uint32_t a, uint32_t b) {
    if (b >= 32u)
        return (a & 0x80000000u) ? 0xffffffffu : 0u;
    return (uint32_t)((int32_t)a >> b);
}

static PyObject *run_chunk(PyObject *self, PyObject *args) {
    PyObject *machine, *halt_obj, *ret;
    State s;
    Decoded *d = NULL;
    uint64_t step_limit;
    uint32_t halt = 0;
    uint32_t previous = 0;
    uint32_t rhs, addr, next;
    int has_halt, stopped = 0;
    int error = 0;
    (void)self;

    if (!PyArg_ParseTuple(args, "OOK:run_chunk", &machine, &halt_obj, &step_limit))
        return NULL;
    has_halt = halt_obj != Py_None;
    if (has_halt) {
        halt = (uint32_t)PyLong_AsUnsignedLongMask(halt_obj);
        if (PyErr_Occurred()) return NULL;
    }
    if (load_state(&s, machine) < 0) {
        release_state(&s);
        return NULL;
    }

#define FINISH_INSN(newpc) do { \
    uint32_t _np = (uint32_t)(newpc); \
    s.regs[0] = 0; \
    s.pc = _np; \
    ++s.steps; \
    if (DYN_UNLIKELY(s.pc == previous)) { stopped = 1; goto done; } \
    if (DYN_UNLIKELY(s.steps >= step_limit)) goto done; \
    if (DYN_UNLIKELY(has_halt && s.pc == halt)) { stopped = 1; goto done; } \
    previous = s.pc; \
    d = decoded_at(&s, s.pc); \
    goto dispatch; \
} while (0)

#define REQUIRE_CMP() do { \
    if (DYN_UNLIKELY(!s.comparison_valid)) { \
        PyErr_SetString(PyExc_RuntimeError, "conditional branch without comparison"); \
        error = 1; goto done; \
    } \
} while (0)

#define BAD_PERSISTENT() do { \
    if (DYN_UNLIKELY(!s.has_persistent)) { \
        PyErr_SetString(PyExc_RuntimeError, "persistent memory is not configured"); \
        error = 1; goto done; \
    } \
} while (0)

    if (s.steps >= step_limit) goto done;
    if (has_halt && s.pc == halt) { stopped = 1; goto done; }
    previous = s.pc;
    d = decoded_at(&s, s.pc);

#if defined(__GNUC__) || defined(__clang__)
    {
        static void *const labels[U_COUNT] = {
            &&L_INVALID, &&L_NOP, &&L_IN, &&L_OUT_R, &&L_OUT_I, &&L_KEY,
            &&L_SCREEN_R, &&L_SCREEN_I, &&L_TIME_LO, &&L_TIME_HI, &&L_GETPC,
            &&L_NAND_RR, &&L_OR_RR, &&L_AND_RR, &&L_NOR_RR, &&L_ADD_RR, &&L_SUB_RR,
            &&L_XOR_RR, &&L_SHL_RR, &&L_SHR_RR, &&L_SAR_RR, &&L_CMP_RR,
            &&L_NAND_RI, &&L_OR_RI, &&L_AND_RI, &&L_NOR_RI, &&L_ADD_RI, &&L_SUB_RI,
            &&L_XOR_RI, &&L_SHL_RI, &&L_SHR_RI, &&L_SAR_RI, &&L_CMP_RI,
            &&L_JEQ_R, &&L_JNE_R, &&L_JLTU_R, &&L_JGEU_R, &&L_JLEU_R, &&L_JGTU_R,
            &&L_JLTS_R, &&L_JGES_R, &&L_JLES_R, &&L_JGTS_R, &&L_JMP_R,
            &&L_JEQ_I, &&L_JNE_I, &&L_JLTU_I, &&L_JGEU_I, &&L_JLEU_I, &&L_JGTU_I,
            &&L_JLTS_I, &&L_JGES_I, &&L_JLES_I, &&L_JGTS_I, &&L_JMP_I,
            &&L_LOAD8_R, &&L_LOAD16_R, &&L_LOAD32_R, &&L_PLOAD32_R,
            &&L_STORE8_R, &&L_STORE16_R, &&L_STORE32_R, &&L_PSTORE32_R,
            &&L_LOAD8_I, &&L_LOAD16_I, &&L_LOAD32_I, &&L_PLOAD32_I,
            &&L_STORE8_I, &&L_STORE16_I, &&L_STORE32_I, &&L_PSTORE32_I
        };

dispatch:
        goto *labels[d->uop];

L_INVALID:
        if (d->opcode >= 0x40 && d->opcode <= 0x5f) {
            if (!s.comparison_valid)
                PyErr_SetString(PyExc_RuntimeError, "conditional branch without comparison");
            else
                PyErr_Format(PyExc_RuntimeError, "unknown branch %#x", d->opcode);
        } else {
            PyErr_Format(PyExc_RuntimeError, "unsupported opcode %#x at %#x", d->opcode, previous);
        }
        error = 1; goto done;
L_NOP:      FINISH_INSN(d->next_pc);
L_IN:       if (queue_pop(s.inputs, &s.regs[d->a]) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_OUT_R:    if (append_u32(s.outputs, s.regs[d->a]) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_OUT_I:    if (append_u32(s.outputs, d->imm) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_KEY:      if (queue_pop(s.keyboard_inputs, &s.regs[d->a]) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_SCREEN_R: if (append_screen(s.screen_updates, s.regs[d->a], s.regs[d->c]) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_SCREEN_I: if (append_screen(s.screen_updates, s.regs[d->a], d->imm) < 0) { error = 1; goto done; } FINISH_INSN(d->next_pc);
L_TIME_LO:  s.regs[d->a] = (uint32_t)s.time_value; FINISH_INSN(d->next_pc);
L_TIME_HI:  s.regs[d->a] = (uint32_t)(s.time_value >> 32); FINISH_INSN(d->next_pc);
L_GETPC:    s.regs[d->a] = previous; if (d->a == 15) s.comparison_valid = 0; FINISH_INSN(d->next_pc);

#define RR_BIN(label, expr) label: rhs = s.regs[d->c]; s.regs[d->a] = (expr); if (d->a == 15) s.comparison_valid = 0; FINISH_INSN(d->next_pc)
#define RI_BIN(label, expr) label: rhs = d->imm;       s.regs[d->a] = (expr); if (d->a == 15) s.comparison_valid = 0; FINISH_INSN(d->next_pc)
        RR_BIN(L_NAND_RR, ~(s.regs[d->b] & rhs));
        RR_BIN(L_OR_RR,    s.regs[d->b] | rhs);
        RR_BIN(L_AND_RR,   s.regs[d->b] & rhs);
        RR_BIN(L_NOR_RR,  ~(s.regs[d->b] | rhs));
        RR_BIN(L_ADD_RR,   s.regs[d->b] + rhs);
        RR_BIN(L_SUB_RR,   s.regs[d->b] - rhs);
        RR_BIN(L_XOR_RR,   s.regs[d->b] ^ rhs);
        RR_BIN(L_SHL_RR,   rhs < 32u ? s.regs[d->b] << rhs : 0u);
        RR_BIN(L_SHR_RR,   rhs < 32u ? s.regs[d->b] >> rhs : 0u);
        RR_BIN(L_SAR_RR,   sar32(s.regs[d->b], rhs));
L_CMP_RR:  if (d->a == 15u) { s.comparison_valid = 1; s.comparison_a = s.regs[d->b]; s.comparison_b = s.regs[d->c]; } s.regs[d->a] = 0; FINISH_INSN(d->next_pc);

        RI_BIN(L_NAND_RI, ~(s.regs[d->b] & rhs));
        RI_BIN(L_OR_RI,    s.regs[d->b] | rhs);
        RI_BIN(L_AND_RI,   s.regs[d->b] & rhs);
        RI_BIN(L_NOR_RI,  ~(s.regs[d->b] | rhs));
        RI_BIN(L_ADD_RI,   s.regs[d->b] + rhs);
        RI_BIN(L_SUB_RI,   s.regs[d->b] - rhs);
        RI_BIN(L_XOR_RI,   s.regs[d->b] ^ rhs);
        RI_BIN(L_SHL_RI,   rhs < 32u ? s.regs[d->b] << rhs : 0u);
        RI_BIN(L_SHR_RI,   rhs < 32u ? s.regs[d->b] >> rhs : 0u);
        RI_BIN(L_SAR_RI,   sar32(s.regs[d->b], rhs));
L_CMP_RI:  s.comparison_valid = 1; s.comparison_a = s.regs[d->b]; s.comparison_b = d->imm; s.regs[d->a] = 0; FINISH_INSN(d->next_pc);
#undef RR_BIN
#undef RI_BIN

#define BR_R(label, cond) label: REQUIRE_CMP(); next = (cond) ? s.regs[d->c] : d->next_pc; FINISH_INSN(next)
#define BR_I(label, cond) label: REQUIRE_CMP(); next = (cond) ? d->imm       : d->next_pc; FINISH_INSN(next)
L_JEQ_R: next = (s.comparison_valid ? s.comparison_a == s.comparison_b : (s.regs[15] & 1u) != 0u) ? s.regs[d->c] : d->next_pc; FINISH_INSN(next);
L_JNE_R: next = (s.comparison_valid ? s.comparison_a != s.comparison_b : (s.regs[15] & 1u) == 0u) ? s.regs[d->c] : d->next_pc; FINISH_INSN(next);
        BR_R(L_JLTU_R, s.comparison_a <  s.comparison_b);
        BR_R(L_JGEU_R, s.comparison_a >= s.comparison_b);
        BR_R(L_JLEU_R, s.comparison_a <= s.comparison_b);
        BR_R(L_JGTU_R, s.comparison_a >  s.comparison_b);
        BR_R(L_JLTS_R, (int32_t)s.comparison_a <  (int32_t)s.comparison_b);
        BR_R(L_JGES_R, (int32_t)s.comparison_a >= (int32_t)s.comparison_b);
        BR_R(L_JLES_R, (int32_t)s.comparison_a <= (int32_t)s.comparison_b);
        BR_R(L_JGTS_R, (int32_t)s.comparison_a >  (int32_t)s.comparison_b);
L_JMP_R: next = s.regs[d->c]; FINISH_INSN(next);
L_JEQ_I: next = (s.comparison_valid ? s.comparison_a == s.comparison_b : (s.regs[15] & 1u) != 0u) ? d->imm : d->next_pc; FINISH_INSN(next);
L_JNE_I: next = (s.comparison_valid ? s.comparison_a != s.comparison_b : (s.regs[15] & 1u) == 0u) ? d->imm : d->next_pc; FINISH_INSN(next);
        BR_I(L_JLTU_I, s.comparison_a <  s.comparison_b);
        BR_I(L_JGEU_I, s.comparison_a >= s.comparison_b);
        BR_I(L_JLEU_I, s.comparison_a <= s.comparison_b);
        BR_I(L_JGTU_I, s.comparison_a >  s.comparison_b);
        BR_I(L_JLTS_I, (int32_t)s.comparison_a <  (int32_t)s.comparison_b);
        BR_I(L_JGES_I, (int32_t)s.comparison_a >= (int32_t)s.comparison_b);
        BR_I(L_JLES_I, (int32_t)s.comparison_a <= (int32_t)s.comparison_b);
        BR_I(L_JGTS_I, (int32_t)s.comparison_a >  (int32_t)s.comparison_b);
L_JMP_I: next = d->imm; FINISH_INSN(next);
#undef BR_R
#undef BR_I

L_LOAD8_R:  addr=s.regs[d->c]; s.regs[d->a]=mem8(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_LOAD16_R: addr=s.regs[d->c]; s.regs[d->a]=mem16be(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_LOAD32_R: addr=s.regs[d->c]; s.regs[d->a]=mem32be(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_PLOAD32_R: BAD_PERSISTENT(); addr=s.regs[d->c]; s.regs[d->a]=mem32be(s.persistent,s.persistent_mask,addr); FINISH_INSN(d->next_pc);
L_STORE8_R: addr=s.regs[d->c]; store8(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,1); FINISH_INSN(d->next_pc);
L_STORE16_R: addr=s.regs[d->c]; store16be(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,2); FINISH_INSN(d->next_pc);
L_STORE32_R: addr=s.regs[d->c]; store32be(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,4); FINISH_INSN(d->next_pc);
L_PSTORE32_R: BAD_PERSISTENT(); addr=s.regs[d->c]; store32be(s.persistent,s.persistent_mask,addr,s.regs[d->a]); FINISH_INSN(d->next_pc);

L_LOAD8_I:  addr=d->imm; s.regs[d->a]=mem8(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_LOAD16_I: addr=d->imm; s.regs[d->a]=mem16be(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_LOAD32_I: addr=d->imm; s.regs[d->a]=mem32be(s.memory,s.mask,addr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);
L_PLOAD32_I: BAD_PERSISTENT(); addr=d->imm; s.regs[d->a]=mem32be(s.persistent,s.persistent_mask,addr); FINISH_INSN(d->next_pc);
L_STORE8_I: addr=d->imm; store8(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,1); FINISH_INSN(d->next_pc);
L_STORE16_I: addr=d->imm; store16be(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,2); FINISH_INSN(d->next_pc);
L_STORE32_I: addr=d->imm; store32be(s.memory,s.mask,addr,s.regs[d->a]); invalidate_decode(&s,addr,4); FINISH_INSN(d->next_pc);
L_PSTORE32_I: BAD_PERSISTENT(); addr=d->imm; store32be(s.persistent,s.persistent_mask,addr,s.regs[d->a]); FINISH_INSN(d->next_pc);
    }
#else
    /* MSVC/portable fallback. Still benefits from predecode and specialized uops. */
    for (;;) {
dispatch:
        switch ((UOp)d->uop) {
            case U_INVALID: if(d->opcode>=0x40&&d->opcode<=0x5f){if(!s.comparison_valid)PyErr_SetString(PyExc_RuntimeError,"conditional branch without comparison");else PyErr_Format(PyExc_RuntimeError,"unknown branch %#x",d->opcode);}else PyErr_Format(PyExc_RuntimeError, "unsupported opcode %#x at %#x", d->opcode, previous); error=1; goto done;
            case U_NOP: FINISH_INSN(d->next_pc);
            case U_IN: if(queue_pop(s.inputs,&s.regs[d->a])<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_OUT_R: if(append_u32(s.outputs,s.regs[d->a])<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_OUT_I: if(append_u32(s.outputs,d->imm)<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_KEY: if(queue_pop(s.keyboard_inputs,&s.regs[d->a])<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_SCREEN_R: if(append_screen(s.screen_updates,s.regs[d->a],s.regs[d->c])<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_SCREEN_I: if(append_screen(s.screen_updates,s.regs[d->a],d->imm)<0){error=1;goto done;} FINISH_INSN(d->next_pc);
            case U_TIME_LO: s.regs[d->a]=(uint32_t)s.time_value; FINISH_INSN(d->next_pc);
            case U_TIME_HI: s.regs[d->a]=(uint32_t)(s.time_value>>32); FINISH_INSN(d->next_pc);
            case U_GETPC: s.regs[d->a]=previous; if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc);

#define SW_RR(u, expr) case u: rhs=s.regs[d->c]; s.regs[d->a]=(expr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc)
#define SW_RI(u, expr) case u: rhs=d->imm; s.regs[d->a]=(expr); if(d->a==15)s.comparison_valid=0; FINISH_INSN(d->next_pc)
            SW_RR(U_NAND_RR, ~(s.regs[d->b]&rhs)); SW_RR(U_OR_RR,s.regs[d->b]|rhs); SW_RR(U_AND_RR,s.regs[d->b]&rhs); SW_RR(U_NOR_RR,~(s.regs[d->b]|rhs));
            SW_RR(U_ADD_RR,s.regs[d->b]+rhs); SW_RR(U_SUB_RR,s.regs[d->b]-rhs); SW_RR(U_XOR_RR,s.regs[d->b]^rhs); SW_RR(U_SHL_RR,rhs<32u?s.regs[d->b]<<rhs:0u); SW_RR(U_SHR_RR,rhs<32u?s.regs[d->b]>>rhs:0u); SW_RR(U_SAR_RR,sar32(s.regs[d->b],rhs));
            case U_CMP_RR: if(d->a==15u){s.comparison_valid=1;s.comparison_a=s.regs[d->b];s.comparison_b=s.regs[d->c];} s.regs[d->a]=0;FINISH_INSN(d->next_pc);
            SW_RI(U_NAND_RI, ~(s.regs[d->b]&rhs)); SW_RI(U_OR_RI,s.regs[d->b]|rhs); SW_RI(U_AND_RI,s.regs[d->b]&rhs); SW_RI(U_NOR_RI,~(s.regs[d->b]|rhs));
            SW_RI(U_ADD_RI,s.regs[d->b]+rhs); SW_RI(U_SUB_RI,s.regs[d->b]-rhs); SW_RI(U_XOR_RI,s.regs[d->b]^rhs); SW_RI(U_SHL_RI,rhs<32u?s.regs[d->b]<<rhs:0u); SW_RI(U_SHR_RI,rhs<32u?s.regs[d->b]>>rhs:0u); SW_RI(U_SAR_RI,sar32(s.regs[d->b],rhs));
            case U_CMP_RI: if(d->a==15u){s.comparison_valid=1;s.comparison_a=s.regs[d->b];s.comparison_b=d->imm;} s.regs[d->a]=0;FINISH_INSN(d->next_pc);
#undef SW_RR
#undef SW_RI

#define SW_BR_R(u, cond) case u: REQUIRE_CMP(); next=(cond)?s.regs[d->c]:d->next_pc; FINISH_INSN(next)
#define SW_BR_I(u, cond) case u: REQUIRE_CMP(); next=(cond)?d->imm:d->next_pc; FINISH_INSN(next)
            case U_JEQ_R: next=(s.comparison_valid?s.comparison_a==s.comparison_b:(s.regs[15]&1u)!=0u)?s.regs[d->c]:d->next_pc;FINISH_INSN(next);
            case U_JNE_R: next=(s.comparison_valid?s.comparison_a!=s.comparison_b:(s.regs[15]&1u)==0u)?s.regs[d->c]:d->next_pc;FINISH_INSN(next);
            SW_BR_R(U_JLTU_R,s.comparison_a<s.comparison_b); SW_BR_R(U_JGEU_R,s.comparison_a>=s.comparison_b); SW_BR_R(U_JLEU_R,s.comparison_a<=s.comparison_b); SW_BR_R(U_JGTU_R,s.comparison_a>s.comparison_b);
            SW_BR_R(U_JLTS_R,(int32_t)s.comparison_a<(int32_t)s.comparison_b); SW_BR_R(U_JGES_R,(int32_t)s.comparison_a>=(int32_t)s.comparison_b); SW_BR_R(U_JLES_R,(int32_t)s.comparison_a<=(int32_t)s.comparison_b); SW_BR_R(U_JGTS_R,(int32_t)s.comparison_a>(int32_t)s.comparison_b);
            case U_JMP_R: next=s.regs[d->c]; FINISH_INSN(next);
            case U_JEQ_I: next=(s.comparison_valid?s.comparison_a==s.comparison_b:(s.regs[15]&1u)!=0u)?d->imm:d->next_pc;FINISH_INSN(next);
            case U_JNE_I: next=(s.comparison_valid?s.comparison_a!=s.comparison_b:(s.regs[15]&1u)==0u)?d->imm:d->next_pc;FINISH_INSN(next);
            SW_BR_I(U_JLTU_I,s.comparison_a<s.comparison_b); SW_BR_I(U_JGEU_I,s.comparison_a>=s.comparison_b); SW_BR_I(U_JLEU_I,s.comparison_a<=s.comparison_b); SW_BR_I(U_JGTU_I,s.comparison_a>s.comparison_b);
            SW_BR_I(U_JLTS_I,(int32_t)s.comparison_a<(int32_t)s.comparison_b); SW_BR_I(U_JGES_I,(int32_t)s.comparison_a>=(int32_t)s.comparison_b); SW_BR_I(U_JLES_I,(int32_t)s.comparison_a<=(int32_t)s.comparison_b); SW_BR_I(U_JGTS_I,(int32_t)s.comparison_a>(int32_t)s.comparison_b);
            case U_JMP_I: next=d->imm; FINISH_INSN(next);
#undef SW_BR_R
#undef SW_BR_I

            case U_LOAD8_R: addr=s.regs[d->c];s.regs[d->a]=mem8(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_LOAD16_R:addr=s.regs[d->c];s.regs[d->a]=mem16be(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_LOAD32_R:addr=s.regs[d->c];s.regs[d->a]=mem32be(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_PLOAD32_R:BAD_PERSISTENT();addr=s.regs[d->c];s.regs[d->a]=mem32be(s.persistent,s.persistent_mask,addr);FINISH_INSN(d->next_pc);
            case U_STORE8_R:addr=s.regs[d->c];store8(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,1);FINISH_INSN(d->next_pc);
            case U_STORE16_R:addr=s.regs[d->c];store16be(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,2);FINISH_INSN(d->next_pc);
            case U_STORE32_R:addr=s.regs[d->c];store32be(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,4);FINISH_INSN(d->next_pc);
            case U_PSTORE32_R:BAD_PERSISTENT();addr=s.regs[d->c];store32be(s.persistent,s.persistent_mask,addr,s.regs[d->a]);FINISH_INSN(d->next_pc);
            case U_LOAD8_I:addr=d->imm;s.regs[d->a]=mem8(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_LOAD16_I:addr=d->imm;s.regs[d->a]=mem16be(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_LOAD32_I:addr=d->imm;s.regs[d->a]=mem32be(s.memory,s.mask,addr);if(d->a==15)s.comparison_valid=0;FINISH_INSN(d->next_pc);
            case U_PLOAD32_I:BAD_PERSISTENT();addr=d->imm;s.regs[d->a]=mem32be(s.persistent,s.persistent_mask,addr);FINISH_INSN(d->next_pc);
            case U_STORE8_I:addr=d->imm;store8(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,1);FINISH_INSN(d->next_pc);
            case U_STORE16_I:addr=d->imm;store16be(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,2);FINISH_INSN(d->next_pc);
            case U_STORE32_I:addr=d->imm;store32be(s.memory,s.mask,addr,s.regs[d->a]);invalidate_decode(&s,addr,4);FINISH_INSN(d->next_pc);
            case U_PSTORE32_I:BAD_PERSISTENT();addr=d->imm;store32be(s.persistent,s.persistent_mask,addr,s.regs[d->a]);FINISH_INSN(d->next_pc);
            default: PyErr_SetString(PyExc_RuntimeError,"internal decoder error");error=1;goto done;
        }
    }
#endif

done:
    if (sync_state(&s) < 0) error = 1;
    if (error) {
        release_state(&s);
        return NULL;
    }
    ret = Py_BuildValue("(iI)", stopped, s.regs[1]);
    release_state(&s);
    return ret;

#undef FINISH_INSN
#undef REQUIRE_CMP
#undef BAD_PERSISTENT
}

static PyMethodDef methods[] = {
    {"run_chunk", run_chunk, METH_VARARGS, "Execute a bounded optimized native instruction batch."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
#ifdef DYN_SYMPHONY
    "_native_symphony",
    "Optimized native Symphony emulator core.",
#else
    "_native",
    "Optimized native Dynphony emulator core.",
#endif
    -1,
    methods
};

#ifdef DYN_SYMPHONY
PyMODINIT_FUNC PyInit__native_symphony(void) {
#else
PyMODINIT_FUNC PyInit__native(void) {
#endif
    return PyModule_Create(&module);
}
