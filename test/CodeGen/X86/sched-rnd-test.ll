; RUN: llc < %s -rng-seed=1 -sched-randomize -sched-randomize-percentage=100 | FileCheck %s --check-prefix=SEED1
; RUN: llc < %s -rng-seed=5 -sched-randomize -sched-randomize-percentage=100 | FileCheck %s --check-prefix=SEED2
; RUN: llc < %s -rng-seed=5 -sched-randomize -sched-randomize-percentage=50 | FileCheck %s --check-prefix=PERCENTAGE

; This test case checks that the schedule randomization is changing
; scheduling decisions, that different seeds result in different
; schedules, and that the percentage alters the amount of
; randomization

define i32 @test(i32 %x, i32 %y, i32 %z) {
entry:
    %a = add i32 %x, %y
    %b = add i32 %x, %z
    %c = add i32 %y, %z
    %d = mul i32 %a, %b
    %e = mul i32 %d, %c
    ret i32 %e
}

; SEED1: leal (%rdi,%rdx), %ecx
; SEED1-NEXT: leal (%rdi,%rsi), %eax
; SEED1-NEXT:	imull %ecx, %eax
; SEED1-NEXT:	addl  %edx, %esi
; SEED1-NEXT:	imull %esi, %eax

; SEED2: leal (%rdi,%rdx), %ecx
; SEED2-NEXT: leal (%rdi,%rsi), %eax
; SEED2-NEXT:	addl  %edx, %esi
; SEED2-NEXT:	imull %ecx, %eax
; SEED2-NEXT:	imull %esi, %eax

; PERCENTAGE: leal  (%rdi,%rsi), %eax
; PERCENTAGE-NEXT:	addl  %edx, %edi
; PERCENTAGE-NEXT:	imull %edi, %eax
; PERCENTAGE-NEXT:	addl  %edx, %esi
; PERCENTAGE-NEXT:	imull %esi, %eax
