;; Dynphony GCC machine description -- integer bootstrap.

(define_constants [(ZR_REG 0) (RET_REG 1) (SP_REG 14) (CMP_REG 15)])

(define_constraint "I"
  "Unsigned 16-bit Dynphony immediate."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 65535)")))

(define_register_constraint "r" "GENERAL_REGS"
  "Dynphony general register")

(define_insn "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,m")
        (match_operand:SI 1 "general_operand"      "r,I,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tmov\t%0, %1\n\tstore_32\t%0, %1"
  [(set_attr "length" "3,4,3")])

(define_insn "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m")
        (match_operand:HI 1 "general_operand"      "r,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tstore_16\t%0, %1"
  [(set_attr "length" "3,3")])

(define_insn "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,m")
        (match_operand:QI 1 "general_operand"      "r,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tstore_8\t%0, %1"
  [(set_attr "length" "3,3")])

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (plus:SI (match_operand:SI 1 "register_operand" "r,r")
                 (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "add\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (minus:SI (match_operand:SI 1 "register_operand" "r,r")
                  (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "sub\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (and:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "and\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ior:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "or\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (xor:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "xor\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ashift:SI (match_operand:SI 1 "register_operand" "r,r")
                   (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "lsl\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (lshiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
                     (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "lsr\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ashiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
                     (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "asr\t%0, %1, %2"
  [(set_attr "length" "3,4")])

;; r15 is ordinary storage outside the compare/branch window.  A compare therefore
;; explicitly clobbers it rather than globally reserving it.
(define_insn "cmpsi"
  [(set (reg:CC CMP_REG)
        (compare:CC (match_operand:SI 0 "register_operand" "r,r")
                    (match_operand:SI 1 "nonmemory_operand" "r,I")))
   (clobber (reg:SI CMP_REG))]
  ""
  "cmp\t%0, %1"
  [(set_attr "length" "3,4")])

(define_expand "cbranchsi4"
  [(set (pc)
        (if_then_else
          (match_operator 0 "ordered_comparison_operator"
            [(match_operand:SI 1 "register_operand")
             (match_operand:SI 2 "nonmemory_operand")])
          (label_ref (match_operand 3 ""))
          (pc)))]
  ""
{
  emit_insn (gen_cmpsi (operands[1], operands[2]));
  rtx cc = gen_rtx_REG (CCmode, CMP_REG);
  rtx cond = gen_rtx_fmt_ee (GET_CODE (operands[0]), VOIDmode, cc, const0_rtx);
  emit_jump_insn (gen_dynphony_branch (cond, operands[3]));
  DONE;
})

(define_insn "dynphony_branch"
  [(set (pc)
        (if_then_else
          (match_operator 0 "comparison_operator"
            [(reg:CC CMP_REG) (const_int 0)])
          (label_ref (match_operand 1 "" ""))
          (pc)))]
  ""
{
  switch (GET_CODE (operands[0]))
    {
    case EQ:  return "je\t%l1";
    case NE:  return "jne\t%l1";
    case LT:  return "jl\t%l1";
    case LE:  return "jle\t%l1";
    case GT:  return "jg\t%l1";
    case GE:  return "jge\t%l1";
    case LTU: return "jb\t%l1";
    case LEU: return "jbe\t%l1";
    case GTU: return "ja\t%l1";
    case GEU: return "jae\t%l1";
    default: gcc_unreachable ();
    }
}
  [(set_attr "length" "4")])

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "jmp\t%l0"
  [(set_attr "length" "4")])

(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))]
  ""
  "jmp\t%0"
  [(set_attr "length" "3")])

(define_expand "prologue" [(const_int 0)] "" { dynphony_expand_prologue (); DONE; })
(define_expand "epilogue" [(const_int 0)] "" { dynphony_expand_epilogue (); DONE; })

;; The assembler spelling expands to the current r14/r15 convention.  GCC sees
;; the stack-pointer and r15 clobbers so scheduling/allocation remains correct.
(define_insn "call"
  [(call (mem:SI (match_operand:SI 0 "general_operand" "r"))
         (match_operand 1 "" ""))
   (clobber (reg:SI CMP_REG))
   (clobber (reg:SI SP_REG))]
  ""
  "call\t%0"
  [(set_attr "length" "16")])

(define_insn "call_value"
  [(set (match_operand 0 "register_operand" "=r")
        (call (mem:SI (match_operand:SI 1 "general_operand" "r"))
              (match_operand 2 "" "")))
   (clobber (reg:SI CMP_REG))
   (clobber (reg:SI SP_REG))]
  ""
  "call\t%1"
  [(set_attr "length" "16")])

(define_insn "return"
  [(return)
   (clobber (reg:SI CMP_REG))
   (clobber (reg:SI SP_REG))]
  ""
  "ret"
  [(set_attr "length" "10")])

(define_attr "length" "1,2,3,4,10,16" (const_int 3))
