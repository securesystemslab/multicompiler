; RUN: llc < %s -mtriple=x86_64-linux -noop-insertion | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-linux -noop-insertion -rng-seed=1 | FileCheck %s --check-prefix=SEED1
; RUN: llc < %s -mtriple=x86_64-linux -noop-insertion -rng-seed=20 | FileCheck %s --check-prefix=SEED2
; RUN: llc < %s -mtriple=x86_64-linux -noop-insertion -rng-seed=153 | FileCheck %s --check-prefix=SEED3

; This test case checks that NOOPs are inserted, and that the RNG seed
; affects both the placement (position of imull) and choice of these NOOPs.

; CHECK: movq %rsp, %rsp
; CHECK: imull
; CHECK: movq %rsp, %rsp
; CHECK-NOT: nop

; SEED1: leaq (%rsi), %rsi
; SEED1: imull
; SEED1: leaq (%rsi), %rsi
; SEED1: retq

; SEED2: imull
; SEED2: movq %rsp, %rsp
; SEED2: leaq (%rsi), %rsi
; SEED2: retq

; SEED3: imull
; SEED3: movq %rbp, %rbp
; SEED3: retq

define i32 @test1(i32 %x, i32 %y, i32 %z) {
entry:
    %tmp = mul i32 %x, %y
    %tmp2 = add i32 %tmp, %z
    ret i32 %tmp2
}
