#ifndef GCC_DYNPHONY_PROTOS_H
#define GCC_DYNPHONY_PROTOS_H

extern HOST_WIDE_INT dynphony_initial_elimination_offset (int, int);
extern void dynphony_expand_prologue (void);
extern void dynphony_expand_epilogue (void);
extern void dynphony_print_operand (FILE *, rtx, int);
extern void dynphony_print_operand_address (FILE *, machine_mode, rtx);
extern bool dynphony_legitimate_address_p (machine_mode, rtx, bool);
extern rtx dynphony_legitimize_address (rtx, rtx, machine_mode);
extern const char *dynphony_output_branch (rtx, rtx);

#endif
