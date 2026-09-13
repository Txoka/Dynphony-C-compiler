/* GCC Dynphony target hooks -- bootstrap implementation.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "memmodel.h"
#include "tm_p.h"
#include "emit-rtl.h"
#include "explow.h"
#include "expr.h"
#include "function.h"
#include "regs.h"
#include "df.h"
#include "output.h"
#include "recog.h"
#include "diagnostic-core.h"
#include "stor-layout.h"
#include "calls.h"
#include "varasm.h"
#include "target-def.h"

/* ABI v1 deliberately mirrors dyncc while the GCC port is validated. */
static const unsigned dynphony_arg_regs[] = { 1, 2, 3, 4, 5, 6 };

struct GTY(()) dynphony_args
{
  unsigned int words;
};

static rtx
dynphony_function_arg (cumulative_args_t cum_v, const function_arg_info &arg)
{
  dynphony_args *cum = (dynphony_args *) cum_v;
  unsigned words = (arg.mode == BLKmode
                    ? (int_size_in_bytes (arg.type) + 3) / 4
                    : (GET_MODE_SIZE (arg.mode).to_constant () + 3) / 4);
  if (words == 1 && cum->words < ARRAY_SIZE (dynphony_arg_regs))
    return gen_rtx_REG (arg.mode, dynphony_arg_regs[cum->words]);
  return NULL_RTX;
}

static void
dynphony_function_arg_advance (cumulative_args_t cum_v,
                               const function_arg_info &arg)
{
  dynphony_args *cum = (dynphony_args *) cum_v;
  unsigned words = (arg.mode == BLKmode
                    ? (int_size_in_bytes (arg.type) + 3) / 4
                    : (GET_MODE_SIZE (arg.mode).to_constant () + 3) / 4);
  cum->words += MAX (1u, words);
}

static void
dynphony_init_cumulative_args (CUMULATIVE_ARGS *cum,
                                tree, rtx, tree, int)
{
  cum->words = 0;
}

static bool
dynphony_return_in_memory (const_tree type, const_tree)
{
  return int_size_in_bytes (type) > 4;
}

static rtx
dynphony_function_value (const_tree ret_type, const_tree, bool)
{
  machine_mode mode = TYPE_MODE (ret_type);
  return gen_rtx_REG (mode, 1);
}

static rtx
dynphony_libcall_value (machine_mode mode, const_rtx)
{
  return gen_rtx_REG (mode, 1);
}

static bool
dynphony_function_value_regno_p (const unsigned int regno)
{
  return regno == 1;
}

bool
dynphony_legitimate_address_p (machine_mode, rtx x, bool strict)
{
  if (REG_P (x))
    return !strict || REGNO (x) < FIRST_PSEUDO_REGISTER;
  if (CONST_INT_P (x))
    return IN_RANGE (INTVAL (x), 0, 65535);
  return false;
}

rtx
dynphony_legitimize_address (rtx x, rtx, machine_mode)
{
  if (dynphony_legitimate_address_p (Pmode, x, false))
    return x;
  return force_reg (Pmode, x);
}

static bool
dynphony_frame_pointer_required (void)
{
  return crtl->calls_alloca || crtl->has_nonlocal_goto;
}

HOST_WIDE_INT
dynphony_initial_elimination_offset (int from, int to)
{
  gcc_assert (to == STACK_POINTER_REGNUM);
  if (from == FRAME_POINTER_REGNUM || from == ARG_POINTER_REGNUM)
    return get_frame_size ();
  gcc_unreachable ();
}

/* These are intentionally conservative first-pass frame sequences.  The .md
   patterns keep them visible to GCC rather than hiding stack mutation in asm. */
void
dynphony_expand_prologue (void)
{
  HOST_WIDE_INT size = get_frame_size ();
  if (size)
    emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
                           GEN_INT (-size)));
}

void
dynphony_expand_epilogue (void)
{
  HOST_WIDE_INT size = get_frame_size ();
  if (size)
    emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
                           GEN_INT (size)));
  emit_jump_insn (gen_return ());
}

void
dynphony_print_operand (FILE *file, rtx x, int code)
{
  if (code == 0)
    {
      if (REG_P (x))
        fputs (reg_names[REGNO (x)], file);
      else if (CONST_INT_P (x))
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      else
        output_addr_const (file, x);
      return;
    }
  output_operand_lossage ("unsupported Dynphony operand modifier");
}

void
dynphony_print_operand_address (FILE *file, machine_mode, rtx addr)
{
  if (REG_P (addr))
    fprintf (file, "[%s]", reg_names[REGNO (addr)]);
  else if (CONST_INT_P (addr))
    fprintf (file, "[" HOST_WIDE_INT_PRINT_DEC "]", INTVAL (addr));
  else
    {
      fputc ('[', file);
      output_addr_const (file, addr);
      fputc (']', file);
    }
}

static bool
dynphony_hard_regno_mode_ok (unsigned int regno, machine_mode mode)
{
  if (regno == 0)
    return false;
  return GET_MODE_SIZE (mode).to_constant () <= 4;
}

static unsigned int
dynphony_hard_regno_nregs (unsigned int, machine_mode mode)
{
  return (GET_MODE_SIZE (mode).to_constant () + 3) / 4;
}

static scalar_int_mode
dynphony_c_mode_for_floating_type (enum tree_index)
{
  return SImode;
}

#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG dynphony_function_arg
#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE dynphony_function_arg_advance
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY dynphony_return_in_memory
#undef TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE dynphony_function_value
#undef TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE dynphony_libcall_value
#undef TARGET_FUNCTION_VALUE_REGNO_P
#define TARGET_FUNCTION_VALUE_REGNO_P dynphony_function_value_regno_p
#undef TARGET_FRAME_POINTER_REQUIRED
#define TARGET_FRAME_POINTER_REQUIRED dynphony_frame_pointer_required
#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P dynphony_legitimate_address_p
#undef TARGET_LEGITIMIZE_ADDRESS
#define TARGET_LEGITIMIZE_ADDRESS dynphony_legitimize_address
#undef TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND dynphony_print_operand
#undef TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS dynphony_print_operand_address
#undef TARGET_HARD_REGNO_MODE_OK
#define TARGET_HARD_REGNO_MODE_OK dynphony_hard_regno_mode_ok
#undef TARGET_HARD_REGNO_NREGS
#define TARGET_HARD_REGNO_NREGS dynphony_hard_regno_nregs

struct gcc_target targetm = TARGET_INITIALIZER;
