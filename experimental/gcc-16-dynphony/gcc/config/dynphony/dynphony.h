/* GCC Dynphony target -- bootstrap target header.
   Hardware: 16 x 32-bit encoded registers; r0 is hardwired zero.
   ABI choices here intentionally mirror dyncc initially.  */

#ifndef GCC_DYNPHONY_H
#define GCC_DYNPHONY_H

#define TARGET_BIG_ENDIAN_DEFAULT 1
#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 1
#define WORDS_BIG_ENDIAN 1

#define BITS_PER_WORD 32
#define UNITS_PER_WORD 4
#define POINTER_SIZE 32
#define PARM_BOUNDARY 32
#define STACK_BOUNDARY 32
#define FUNCTION_BOUNDARY 8
#define BIGGEST_ALIGNMENT 32
#define STRICT_ALIGNMENT 0

#define FIRST_PSEUDO_REGISTER 16

/* r0 is hardware zero. r14 is the ABI stack pointer, not an ISA requirement.
   r12 is kept fixed as the initial frame-pointer policy. */
#define FIXED_REGISTERS \
{ 1,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0 }

/* 1 means clobbered by an ordinary public-ABI call.  r15 is intentionally
   allocatable, but comparison/call patterns explicitly clobber it. */
#define CALL_USED_REGISTERS \
{ 1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,1 }

#define REGISTER_NAMES \
{ "zr","r1","r2","r3","r4","r5","r6","r7", \
  "r8","r9","r10","r11","r12","r13","sp","flags" }

#define STACK_POINTER_REGNUM 14
#define HARD_FRAME_POINTER_REGNUM 12
#define FRAME_POINTER_REGNUM 12
#define ARG_POINTER_REGNUM 12
#define STATIC_CHAIN_REGNUM 7

#define ELIMINABLE_REGS \
{{ FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM }, \
 { ARG_POINTER_REGNUM, STACK_POINTER_REGNUM }}

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  ((OFFSET) = dynphony_initial_elimination_offset ((FROM), (TO)))

#define RETURN_VALUE_REGNUM 1

#define DEFAULT_PCC_STRUCT_RETURN 0

#define FUNCTION_ARG_REGNO_P(N) ((N) >= 1 && (N) <= 6)

#define REGNO_REG_CLASS(R) GENERAL_REGS
#define BASE_REG_CLASS GENERAL_REGS
#define INDEX_REG_CLASS NO_REGS
#define REGNO_OK_FOR_BASE_P(R) ((R) > 0 && (R) < FIRST_PSEUDO_REGISTER)
#define MAX_REGS_PER_ADDRESS 1

#define LEGITIMATE_CONSTANT_P(X) 1

#define MOVE_MAX 4
#define MOVE_RATIO(SPEED) 2
#define SLOW_BYTE_ACCESS 0

#define Pmode SImode
#define FUNCTION_MODE SImode

#define ASM_COMMENT_START ";"
#define ASM_APP_ON ""
#define ASM_APP_OFF ""

#define GLOBAL_ASM_OP ".global\t"
#define TEXT_SECTION_ASM_OP ".text"
#define DATA_SECTION_ASM_OP ".data"
#define BSS_SECTION_ASM_OP ".bss"

#define ASM_OUTPUT_ALIGN(FILE, LOG) \
  do { if ((LOG) != 0) fprintf ((FILE), "\t.align\t%d\n", 1 << (LOG)); } while (0)

#define TRAMPOLINE_SIZE 0

#endif /* GCC_DYNPHONY_H */
