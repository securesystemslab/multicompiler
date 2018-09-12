//===-- llvm/Trampoline.h - Class to represent a single trampoline --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Trampoline class, which represents a
// single trampoline in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TRAMPOLINE_H
#define LLVM_IR_TRAMPOLINE_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"

namespace llvm {

class FunctionType;
class LLVMContext;

class Trampoline : public GlobalObject, public ilist_node<Trampoline> {
  friend class SymbolTableListTraits<Trampoline>;
  void operator=(const Trampoline &)  = delete;
  Trampoline(const Trampoline &)  = delete;

protected:
  Trampoline(LLVMContext &C, ValueTy VTy, Use *Ops, unsigned NumOps,
             LinkageTypes Linkage, const Twine &name = "",
             Module *ParentModule = nullptr);

public:
  static Trampoline *Create(Constant *I);
  static Trampoline *Create(Instruction *I);

  void setParent(Module *parent);
  void removeFromParent() override;
  void eraseFromParent() override;

  inline Type *getType() const {
    return Value::getType();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const Value *V) {
    return V->getValueID() == Value::JumpTrampolineVal ||
           V->getValueID() == Value::CallTrampolineVal;
  }
};

class JumpTrampoline : public Trampoline {
  void *operator new(size_t, unsigned)  = delete;
  void operator=(const JumpTrampoline &)  = delete;
  JumpTrampoline(const JumpTrampoline &)  = delete;

  JumpTrampoline(GlobalValue *Target);
  JumpTrampoline(LLVMContext &C);
  JumpTrampoline(LinkageTypes Linkage, const Twine &Name,
                 Module *Parent);

public:
  // allocate space for exactly one operand
  void *operator new(size_t s) {
    return User::operator new(s, 1);
  }

  static JumpTrampoline *Create(GlobalValue *Target) {
    return new JumpTrampoline(Target);
  }
  static JumpTrampoline *Create(Constant *Target) {
    // TODO simplify this, too complicated
    GlobalValue *F = dyn_cast<GlobalValue>(Target);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Target)) {
      if (CE->getOpcode() == Instruction::BitCast) {
        F = dyn_cast<GlobalValue>(CE->getOperand(0));
      }
    }

    if (F)
      return new JumpTrampoline(F);
    else
      return new JumpTrampoline(Target->getContext());
  }

  static JumpTrampoline *Create(LLVMContext &C) {
    return new JumpTrampoline(C);
  }

  static JumpTrampoline *Create(LinkageTypes Linkage, const Twine &Name,
                                Module *Parent) {
    return new JumpTrampoline(Linkage, Name, Parent);
  }

private:
  GlobalValue *getTargetImpl() const {
    Value *V = Op<0>().get();
    if (!V)
      return nullptr;
    GlobalValue *F = dyn_cast<GlobalValue>(V);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
      if (CE->getOpcode() == Instruction::BitCast) {
        F = dyn_cast<GlobalValue>(CE->getOperand(0));
      }
    }
    if (!F) {
      dbgs() << "Could not handle trampoline for value: ";
      if (V)
        V->dump();
      else
        dbgs() << "(nullptr)\n";
    };
    return F;
  }

public:
  const GlobalValue *getTarget() const {
    return getTargetImpl();
  }

  GlobalValue *getTarget() {
    return getTargetImpl();
  }

  void setTarget(Constant *Target) {
    setOperand(0, Target);
    if (!Target)
      setGlobalVariableNumOperands(0);
  }

  Value *handleOperandChangeImpl(Value *From, Value *To, Use *U);
  void destroyConstantImpl();

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const Value *V) {
    return V->getValueID() == Value::JumpTrampolineVal;
  }
};

template <>
struct OperandTraits<JumpTrampoline> :
  public OptionalOperandTraits<JumpTrampoline> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(JumpTrampoline, Value)

class CallTrampoline : public Trampoline {
private:
  CallSite CS;

  void operator=(const CallTrampoline &)  = delete;
  CallTrampoline(const CallTrampoline &)  = delete;

  CallTrampoline(CallSite CS);

public:
  static CallTrampoline *Create(Instruction *I) {
    CallSite CS(I);
    if (CS)
      return new(0) CallTrampoline(CS);
    else
      return nullptr;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static inline bool classof(const Value *V) {
    return V->getValueID() == Value::CallTrampolineVal;
  }
};

}

#endif

