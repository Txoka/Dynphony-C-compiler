#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>

typedef struct {
    PyObject *machine;
    PyObject *regs_obj;
    PyObject *inputs;
    PyObject *keyboard_inputs;
    PyObject *outputs;
    PyObject *screen_updates;
    PyObject *comparison_obj;
    PyObject *persistent_obj;
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
} State;

static uint32_t read_be(const uint8_t *memory, uint32_t mask, uint32_t address, int size) {
    uint32_t value = 0;
    int i;
    for (i = 0; i < size; i++)
        value = (value << 8) | memory[(address + (uint32_t)i) & mask];
    return value;
}

static void write_be(uint8_t *memory, uint32_t mask, uint32_t address,
                     uint32_t value, int size) {
    int i;
    for (i = 0; i < size; i++)
        memory[(address + (uint32_t)i) & mask] =
            (uint8_t)(value >> (8 * (size - i - 1)));
}

static PyObject *get_attr(PyObject *object, const char *name) {
    return PyObject_GetAttrString(object, name);
}

static int load_state(State *s, PyObject *machine) {
    PyObject *obj = NULL;
    Py_ssize_t i;
    memset(s, 0, sizeof(*s));
    s->machine = machine;

    obj = get_attr(machine, "memory");
    if (!obj || !PyByteArray_Check(obj)) {
        Py_XDECREF(obj);
        PyErr_SetString(PyExc_TypeError, "machine.memory must be a bytearray");
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
    for (i = 0; i < 16; i++) {
        s->regs[i] = (uint32_t)PyLong_AsUnsignedLongMask(
            PyList_GET_ITEM(s->regs_obj, i));
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
        if (!PyTuple_Check(s->comparison_obj) ||
            PyTuple_GET_SIZE(s->comparison_obj) != 2) {
            PyErr_SetString(PyExc_TypeError, "invalid machine comparison state");
            return -1;
        }
        s->comparison_valid = 1;
        s->comparison_a = (uint32_t)PyLong_AsUnsignedLongMask(
            PyTuple_GET_ITEM(s->comparison_obj, 0));
        s->comparison_b = (uint32_t)PyLong_AsUnsignedLongMask(
            PyTuple_GET_ITEM(s->comparison_obj, 1));
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
    }
    return 0;
}

static void release_state(State *s) {
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
    for (i = 0; i < 16; i++) {
        value = PyLong_FromUnsignedLong(s->regs[i]);
        if (!value) return -1;
        if (PyList_SetItem(s->regs_obj, i, value) < 0) return -1;
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

static int execute_one(State *s) {
    uint32_t pc = s->pc, next_pc = pc + 1, a, right, target, address, value;
    uint8_t op = (uint8_t)read_be(s->memory, s->mask, pc, 1);
    uint8_t pair, dst, left, code, operand, base;
    int immediate, size, take = 0;

    if (op == 0) {
    } else if (op == 1) {
        dst = (uint8_t)(read_be(s->memory, s->mask, pc + 1, 1) >> 4);
        if (queue_pop(s->inputs, &s->regs[dst]) < 0) return -1;
        next_pc = pc + 2;
    } else if (op == 2) {
        operand = (uint8_t)read_be(s->memory, s->mask, pc + 2, 1);
        if (append_u32(s->outputs, s->regs[operand & 15]) < 0) return -1;
        next_pc = pc + 3;
    } else if (op == 0x12) {
        if (append_u32(s->outputs, read_be(s->memory, s->mask, pc + 2, 2)) < 0)
            return -1;
        next_pc = pc + 4;
    } else if (op == 3) {
        dst = (uint8_t)(read_be(s->memory, s->mask, pc + 1, 1) >> 4);
        if (queue_pop(s->keyboard_inputs, &s->regs[dst]) < 0) return -1;
        next_pc = pc + 2;
    } else if (op == 4 || op == 0x14) {
        /* Handled below after the main opcode chain. */
    } else if (op == 5 || op == 6) {
        dst = (uint8_t)(read_be(s->memory, s->mask, pc + 1, 1) >> 4);
        s->regs[dst] = (uint32_t)(op == 5 ? s->time_value : s->time_value >> 32);
        next_pc = pc + 2;
    } else if (op == 7) {
        dst = (uint8_t)(read_be(s->memory, s->mask, pc + 1, 1) >> 4);
        s->regs[dst] = pc;
        if (dst == 15) s->comparison_valid = 0;
        next_pc = pc + 2;
    } else if (op >= 0x20 && op <= 0x3a && (op & 15) <= 10) {
        pair = (uint8_t)read_be(s->memory, s->mask, pc + 1, 1);
        dst = pair >> 4;
        left = pair & 15;
        immediate = (op & 16) != 0;
        right = immediate ? read_be(s->memory, s->mask, pc + 2, 2) :
            s->regs[read_be(s->memory, s->mask, pc + 2, 1) & 15];
        a = s->regs[left];
        code = op & 15;
        next_pc = pc + (immediate ? 4 : 3);
        if (code == 10) {
            s->comparison_valid = 1;
            s->comparison_a = a;
            s->comparison_b = right;
            s->regs[dst] = 0;
        } else {
            switch (code) {
                case 0: value = ~(a & right); break;
                case 1: value = a | right; break;
                case 2: value = a & right; break;
                case 3: value = ~(a | right); break;
                case 4: value = a + right; break;
                case 5: value = a - right; break;
                case 6: value = a ^ right; break;
                case 7: value = right < 32 ? a << right : 0; break;
                case 8: value = right < 32 ? a >> right : 0; break;
                default:
                    value = right >= 32 ? ((a & 0x80000000u) ? 0xffffffffu : 0) :
                        (uint32_t)((int32_t)a >> right);
                    break;
            }
            s->regs[dst] = value;
            if (dst == 15) s->comparison_valid = 0;
        }
    } else if (op >= 0x40 && op <= 0x5f) {
        immediate = (op & 16) != 0;
        base = op & 0xef;
        target = immediate ? read_be(s->memory, s->mask, pc + 2, 2) :
            s->regs[read_be(s->memory, s->mask, pc + 2, 1) & 15];
        next_pc = pc + (immediate ? 4 : 3);
        if (base == 0x48) {
            take = 1;
        } else {
            if (!s->comparison_valid) {
                PyErr_SetString(PyExc_RuntimeError, "conditional branch without comparison");
                return -1;
            }
            a = s->comparison_a;
            right = s->comparison_b;
            switch (base) {
                case 0x41: take = a == right; break;
                case 0x49: take = a != right; break;
                case 0x42: take = a < right; break;
                case 0x4a: take = a >= right; break;
                case 0x43: take = a <= right; break;
                case 0x4b: take = a > right; break;
                case 0x44: take = (int32_t)a < (int32_t)right; break;
                case 0x4c: take = (int32_t)a >= (int32_t)right; break;
                case 0x45: take = (int32_t)a <= (int32_t)right; break;
                case 0x4d: take = (int32_t)a > (int32_t)right; break;
                default:
                    PyErr_Format(PyExc_RuntimeError, "unknown branch %#x", op);
                    return -1;
            }
        }
        if (take) next_pc = target;
    } else if (op >= 0x60 && op <= 0x77) {
        immediate = (op & 16) != 0;
        code = op & 7;
        operand = (uint8_t)read_be(s->memory, s->mask, pc + 1, 1);
        address = immediate ? read_be(s->memory, s->mask, pc + 2, 2) :
            s->regs[read_be(s->memory, s->mask, pc + 2, 1) & 15];
        next_pc = pc + (immediate ? 4 : 3);
        if (code == 3) {
            if (!s->has_persistent) {
                PyErr_SetString(PyExc_RuntimeError, "persistent memory is not configured");
                return -1;
            }
            s->regs[operand >> 4] = read_be(
                s->persistent, s->persistent_mask, address, 4);
        } else if (code == 7) {
            if (!s->has_persistent) {
                PyErr_SetString(PyExc_RuntimeError, "persistent memory is not configured");
                return -1;
            }
            write_be(s->persistent, s->persistent_mask, address,
                     s->regs[operand & 15], 4);
        } else if (code < 4) {
            size = code == 0 ? 1 : (code == 1 ? 2 : 4);
            s->regs[operand >> 4] = read_be(s->memory, s->mask, address, size);
            if ((operand >> 4) == 15) s->comparison_valid = 0;
        } else {
            code &= 3;
            size = code == 0 ? 1 : (code == 1 ? 2 : 4);
            write_be(s->memory, s->mask, address, s->regs[operand & 15], size);
        }
    } else {
        PyErr_Format(PyExc_RuntimeError, "unsupported opcode %#x at %#x", op, pc);
        return -1;
    }

    /* Screen is split out to keep declarations valid in C90-compatible builds. */
    if (op == 4 || op == 0x14) {
        uint32_t setting = s->regs[read_be(s->memory, s->mask, pc + 1, 1) & 15];
        immediate = op == 0x14;
        value = immediate ? read_be(s->memory, s->mask, pc + 2, 2) :
            s->regs[read_be(s->memory, s->mask, pc + 2, 1) & 15];
        if (append_screen(s->screen_updates, setting, value) < 0) return -1;
        next_pc = pc + (immediate ? 4 : 3);
    }

    s->regs[0] = 0;
    s->pc = next_pc;
    s->steps++;
    return 0;
}

static PyObject *run_chunk(PyObject *self, PyObject *args) {
    PyObject *machine, *halt_obj, *value;
    State state;
    uint64_t step_limit;
    uint32_t halt = 0;
    int has_halt, stopped = 0;
    (void)self;
    if (!PyArg_ParseTuple(args, "OOK:run_chunk", &machine, &halt_obj, &step_limit))
        return NULL;
    has_halt = halt_obj != Py_None;
    if (has_halt) {
        halt = (uint32_t)PyLong_AsUnsignedLongMask(halt_obj);
        if (PyErr_Occurred()) return NULL;
    }
    if (load_state(&state, machine) < 0) {
        release_state(&state);
        return NULL;
    }
    while (state.steps < step_limit) {
        uint32_t previous;
        if (has_halt && state.pc == halt) {
            stopped = 1;
            break;
        }
        previous = state.pc;
        if (execute_one(&state) < 0) {
            sync_state(&state);
            release_state(&state);
            return NULL;
        }
        if (state.pc == previous) {
            stopped = 1;
            break;
        }
    }
    if (sync_state(&state) < 0) {
        release_state(&state);
        return NULL;
    }
    value = Py_BuildValue("(iI)", stopped, state.regs[1]);
    release_state(&state);
    return value;
}

static PyMethodDef methods[] = {
    {"run_chunk", run_chunk, METH_VARARGS, "Execute a bounded native instruction batch."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_native",
    "Native Dynphony emulator core.",
    -1,
    methods
};

PyMODINIT_FUNC PyInit__native(void) {
    return PyModule_Create(&module);
}
