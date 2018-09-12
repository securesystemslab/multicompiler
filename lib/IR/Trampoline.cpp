//===-- Trampoline.cpp - Implement the Global object classes ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Trampoline class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Trampoline.h"
#include "llvm/IR/Module.h"
using namespace llvm;

#define DEBUG_TYPE "trampoline"

Trampoline::Trampoline(LLVMContext &C, ValueTy VTy, Use *Ops, unsigned NumOps,
                       LinkageTypes Linkage, const Twine &name,
                       Module *ParentModule) :
  GlobalObject(Type::getTrampolineTy(C), VTy, Ops, NumOps, Linkage, name) {
  if (ParentModule)
    ParentModule->getTrampolineList().push_back(this);
}

Trampoline *Trampoline::Create(Constant *I) {
  return JumpTrampoline::Create(I);
}
Trampoline *Trampoline::Create(Instruction *I) {
  return CallTrampoline::Create(I);
}

void Trampoline::setParent(Module *parent) {
  Parent = parent;
}

void Trampoline::removeFromParent() {
  getParent()->getTrampolineList().remove(this);
}

void Trampoline::eraseFromParent() {
  getParent()->getTrampolineList().erase(this);
}

JumpTrampoline::JumpTrampoline(LLVMContext &C) :
  Trampoline(C, Value::JumpTrampolineVal,
             nullptr, 0,
             GlobalValue::PrivateLinkage, "null_tramp") {
}

JumpTrampoline::JumpTrampoline(GlobalValue *Target) :
  Trampoline(Target->getContext(), Value::JumpTrampolineVal,
             OperandTraits<JumpTrampoline>::op_begin(this), 1,
             Target->getLinkage(), Target->getName() + "_tramp",
             Target->getParent()) {
  Op<0>() = Target;
}

JumpTrampoline::JumpTrampoline(LinkageTypes Linkage, const Twine &Name,
                               Module *Parent) :
  Trampoline(Parent->getContext(), Value::JumpTrampolineVal,
             OperandTraits<JumpTrampoline>::op_begin(this), 1,
             Linkage, Name, Parent) {
  Op<0>() = nullptr;
}

CallTrampoline::CallTrampoline(CallSite CS) :
  Trampoline(CS.getCalledFunction()->getContext(), Value::CallTrampolineVal,
             nullptr, 0, CS.getCalledFunction()->getLinkage(),
             CS.getCalledFunction()->getName() + "_cstramp", CS.getCaller()->getParent()),
  CS(CS) {}

// FIXME : NYO
Value *JumpTrampoline::handleOperandChangeImpl(Value *From, Value *To,
                                               Use *U) {
  // If you call this, then you better know this GVar has a constant
  // initializer worth replacing. Enforce that here.
  assert(getNumOperands() == 1 &&
         "Attempt to replace uses of Constants on a JumpTrampoline with no target");

  // And, since you know it has an initializer, the From value better be
  // the initializer :)
  assert(getOperand(0) == From &&
         "Attempt to replace wrong constant initializer in JumpTrampoline");

  // And, you better have a constant for the replacement value
  assert(isa<Constant>(To) &&
         "Attempt to replace JumpTrampoline target with non-constant");

  DEBUG(dbgs() << "Replacing Trampoline with ");
  DEBUG(To->dump());
  this->setOperand(0, To);
  return nullptr;
}

// FIXME : NYO
void JumpTrampoline::destroyConstantImpl() {
  DEBUG(dbgs() << "Destroying trampoline pointing to: ");
  DEBUG(getOperand(0)->dump());
  Op<0>() = nullptr;
  if (getParent())
    removeFromParent();
}
