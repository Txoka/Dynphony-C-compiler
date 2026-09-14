;; Dynphony GCC machine description.
;; GCC machine description for the Dynphony ISA bootstrap backend.

(define_constants
  [(ZR_REG 0)
   (SP_REG 14)
   (CMP_REG 15)])

(define_constraint "I"
  "Unsigned 16-bit immediate."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 65535)")))

(define_insn "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,m")
        (match_operand:SI 1 "general_operand" "r,I,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tmov\t%0, %1\n\tstore32\t%1, %0"
  [(set_attr "length" "3,4,3")])

(define_insn "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,m")
        (match_operand:HI 1 "general_operand" "r,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tstore16\t%1, %0"
  [(set_attr "length" "3,3")])

(define_insn "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,m")
        (match_operand:QI 1 "general_operand" "r,r"))]
  ""
  "@\n\tmov\t%0, %1\n\tstore8\t%1, %0"
  [(set_attr "length" "3,3")])

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (plus:SI (match_operand:SI 1 "register_operand" "r,r")
                 (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tadd\t%0, %1, %2\n\taddi\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (minus:SI (match_operand:SI 1 "register_operand" "r,r")
                  (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tsub\t%0, %1, %2\n\tsubi\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (and:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tand\t%0, %1, %2\n\tandi\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ior:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tor\t%0, %1, %2\n\tori\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (xor:SI (match_operand:SI 1 "register_operand" "r,r")
                (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\txor\t%0, %1, %2\n\txori\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ashift:SI (match_operand:SI 1 "register_operand" "r,r")
                   (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tlsl\t%0, %1, %2\n\tlsli\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (lshiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
                     (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tlsr\t%0, %1, %2\n\tlsri\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
        (ashiftrt:SI (match_operand:SI 1 "register_operand" "r,r")
                     (match_operand:SI 2 "nonmemory_operand" "r,I")))]
  ""
  "@\n\tasr\t%0, %1, %2\n\tasri\t%0, %1, %2"
  [(set_attr "length" "3,4")])

(define_insn "cmpsi"
  [(set (reg:CC CMP_REG)
        (compare:CC (match_operand:SI 0 "register_operand" "r")
                    (match_operand:SI 1 "nonmemory_operand" "rI")))]
  ""
  "cmp\t%0, %1"
  [(set_attr "length" "3")])

(define_insn "cbranchsi4"
  [(set (pc)
        (if_then_else
          (match_operator 0 "comparison_operator"
            [(match_operand:SI 1 "register_operand" "r")
             (match_operand:SI 2 "nonmemory_operand" "rI")])
          (label_ref (match_operand 3 "" ""))
          (pc)))
   (clobber (reg:SI CMP_REG))]
  ""
  "cmp\t%1, %2\n\tb%0\t%l3"
  [(set_attr "length" "7")])

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

(define_attr "length" "" (const_int 3))
