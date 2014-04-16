; RUN: llc < %s -nop-insertion | FileCheck %s
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=1 | FileCheck %s --check-prefix=SEED1
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=25 | FileCheck %s --check-prefix=SEED2
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=1534 | FileCheck %s --check-prefix=SEED3
; RUN: llc < %s -nop-insertion -entropy-data="different entropy" -rng-seed=1 | FileCheck %s --check-prefix=ENTROPY

; This test case checks that NOPs are inserted, and that the RNG seed
; affects both the placement (position of imull) and choice of these NOPs.

; CHECK: movq %rsp, %rsp
; CHECK: imull
; CHECK: movq %rbp, %rbp
; CHECK-NOT: leaq
; CHECK-NOT: nop

; SEED1: imull
; SEED1: leaq (%rsi), %rsi
; SEED1-NOT: movq
; SEED1-NOT: nop

; SEED2: movq %rbp, %rbp
; SEED2: imull
; SEED2: movq %rbp, %rbp
; SEED2-NOT: leaq
; SEED2-NOT: nop

; SEED3: movq %rsp, %rsp
; SEED3: imull
; SEED3: movq %rsp, %rsp
; SEED3-NOT: leaq
; SEED3-NOT: nop

; ENTROPY: imull
; ENTROPY: nop
; ENTROPY: nop
; ENTROPY-NOT: leaq
; ENTROPY-NOT: movq

define i32 @test1(i32 %x, i32 %y, i32 %z) {
entry:
    %tmp = mul i32 %x, %y
    %tmp2 = add i32 %tmp, %z
    ret i32 %tmp2
}
