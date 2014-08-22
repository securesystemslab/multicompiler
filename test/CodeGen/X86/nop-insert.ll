; RUN: llc < %s -mtriple=x86_64-linux -nop-insertion | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-linux -nop-insertion -rng-seed=1 | FileCheck %s --check-prefix=SEED1
; RUN: llc < %s -mtriple=x86_64-linux -nop-insertion -rng-seed=25 | FileCheck %s --check-prefix=SEED2
; RUN: llc < %s -mtriple=x86_64-linux -nop-insertion -rng-seed=1534 | FileCheck %s --check-prefix=SEED3

; This test case checks that NOPs are inserted, and that the RNG seed
; affects both the placement (position of imull) and choice of these NOPs.

; CHECK: imull
; CHECK: leaq (%rsi), %rsi
; CHECK: retq
; CHECK-NOT: nop

; SEED1: imull
; SEED1: movq %rbp, %rbp
; SEED1: retq
; SEED1-NOT: nop

; SEED2: movq %rsp, %rsp
; SEED2: imull
; SEED2: leaq (%rsi), %rsi
; SEED2: nop
; SEED2: retq

; SEED3: movq %rsp, %rsp
; SEED3: imull
; SEED3: nop
; SEED3: retq

define i32 @test1(i32 %x, i32 %y, i32 %z) {
entry:
    %tmp = mul i32 %x, %y
    %tmp2 = add i32 %tmp, %z
    ret i32 %tmp2
}
