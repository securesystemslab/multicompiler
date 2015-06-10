; RUN: opt < %s -shuffle-functions-pass -S -rng-seed=1 | FileCheck %s --check-prefix=SEED1
; RUN: opt < %s -shuffle-functions-pass -S -rng-seed=2 | FileCheck %s --check-prefix=SEED2
; RUN: opt < %s -shuffle-functions-pass -S -rng-seed=3 | FileCheck %s --check-prefix=SEED3
; RUN: opt < %s -shuffle-functions-pass -S -rng-seed=4 | FileCheck %s --check-prefix=SEED4
; RUN: opt < %s -shuffle-functions-pass -S -rng-seed=5 | FileCheck %s --check-prefix=SEED5

define i32 @func1() {
  foo: ret i32 0
}

define i32 @func2() {
  foo: ret i32 0
}

define i32 @func3() {
  foo: ret i32 0
}

; SEED1: define i32 @func2() {
; SEED1: define i32 @func3() {
; SEED1: define i32 @func1() {

; SEED2: define i32 @func3() {
; SEED2: define i32 @func1() {
; SEED2: define i32 @func2() {

; SEED3: define i32 @func2() {
; SEED3: define i32 @func3() {
; SEED3: define i32 @func1() {

; SEED4: define i32 @func1() {
; SEED4: define i32 @func3() {
; SEED4: define i32 @func2() {

; SEED5: define i32 @func2() {
; SEED5: define i32 @func3() {
; SEED5: define i32 @func1() {

