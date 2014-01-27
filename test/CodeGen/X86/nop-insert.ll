; RUN: llc < %s -nop-insertion | FileCheck %s
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=1 | FileCheck %s --check-prefix=SEED1
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=25 | FileCheck %s --check-prefix=SEED2
; RUN: llc < %s -nop-insertion -entropy-data="test" -rng-seed=1534 | FileCheck %s --check-prefix=SEED3
; RUN: llc < %s -nop-insertion -entropy-data="different entropy" -rng-seed=1 | FileCheck %s --check-prefix=ENTROPY

; This test case checks that NOPs are inserted, and that the RNG seed
; affects both the placement and choice of these NOPs.

; CHECK: leaq	(%rsi), %rsi

; SEED1: nop
; SEED1-NOT: leaq	(%rsi), %rsi

; SEED2: leaq	(%rdi), %rdi
; SEED2: leaq	(%rdi), %rdi
; SEED2-NOT: leaq	(%rsi), %rsi

; SEED3: nop

; ENTROPY: movq    %rbp, %rbp
; ENTROPY: leaq	(%rdi), %rdi
; ENTROPY-NOT: nop

define i32 @test1(i32 %x, i32 %y, i32 %z) {
entry:
    %tmp = mul i32 %x, %y
    %tmp2 = add i32 %tmp, %z
    ret i32 %tmp2
}
