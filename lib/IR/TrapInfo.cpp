//===-- TrapInfo.cpp - Implement TrapInfo class ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/TrapInfo.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/GlobalVariable.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
// TrapInfo Implementation
//===----------------------------------------------------------------------===//

const GlobalVariable *TrapInfo::getClassName() const {
  if (isUnknown())
    return nullptr;
  ValueAsMetadata *ValueMD = cast<ValueAsMetadata>(Info->getOperand(0));
  return cast<GlobalVariable>(ValueMD->getValue());
}

const Metadata *TrapInfo::getClassMD() const {
  if (isUnknown())
    return nullptr;
  return cast<ValueAsMetadata>(Info->getOperand(0));
}

uint64_t TrapInfo::getMaxNumVFuncs() const {
  if (isUnknown())
    return 0;
  auto *CI = mdconst::dyn_extract<ConstantInt>(Info->getOperand(1));
  return CI->getZExtValue();
}

TrapInfo TrapInfo::getVCall(GlobalVariable* ClassName, uint64_t MaxNumVFuncs) {
  SmallVector<Metadata *, 2> MDVals;
  MDVals.push_back(ValueAsMetadata::get(ClassName));
  MDVals.push_back(ValueAsMetadata::getConstant(
                     ConstantInt::get(Type::getInt64Ty(ClassName->getContext()), MaxNumVFuncs)));

  return TrapInfo(MDNode::get(ClassName->getContext(), MDVals));
}

TrapInfo TrapInfo::getMethodPointer(GlobalVariable* ClassName, uint64_t MaxNumVFuncs) {
  TrapInfo TI = getVCall(ClassName, MaxNumVFuncs);
  TI.methodPointer = true;
  return TI;
}

TrapInfo TrapInfo::replaceClassName(GlobalVariable *NewClass) {
  if (methodPointer)
    return getMethodPointer(NewClass, getMaxNumVFuncs());
  else
    return getVCall(NewClass, getMaxNumVFuncs());
}

/*
/// getAsMDNode - This method converts the compressed TrapInfo node into a
/// DILocation-compatible MDNode.
MDNode *TrapInfo::getAsMDNode() const { return Loc; }

/// getFromDILocation - Translate the DILocation quad into a TrapInfo.
TrapInfo TrapInfo::getFromDILocation(MDNode *N) {
  TrapInfo Loc;
  Loc.Loc.reset(N);
  return Loc;
}
*/

void TrapInfo::dump() const {
#ifndef NDEBUG
  if (!isUnknown()) {
    getClassName()->dump();
    dbgs() << "(max " << getMaxNumVFuncs() << " fns)";
    if (methodPointer)
      dbgs() << " in struct";
    dbgs() << "\n";
  }
#endif
}
