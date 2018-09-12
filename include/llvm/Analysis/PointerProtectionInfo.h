//===-- PointerProtectionInfo.h: Info for Jump-Instruction Tables --*- C++ -*-===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Information about jump-instruction tables that have been created by
/// JumpInstrTables pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_POINTERPROTECTIONINFO_H
#define LLVM_ANALYSIS_POINTERPROTECTIONINFO_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Pass.h"

#include <vector>
#include <map>

namespace llvm {
class Function;
class FunctionType;

class PointerProtectionInfo : public ImmutablePass {
public:
  static char ID;

  PointerProtectionInfo();
  virtual ~PointerProtectionInfo();
  const char *getPassName() const override {
    return "Pointer Protection Info";
  }

  typedef MapVector<Function *, Trampoline *> JumpTableMap;
  typedef std::map<CallSite, Function * > CallTableMap;

  /// Inserts an entry in a table, adding the table if it doesn't exist.
  int insertEntry(Function *F, Trampoline *T) {
    auto Result = JumpTable.insert(std::make_pair(F, T));
    return Result.first - JumpTable.begin();
  }

  void insertCallEntry(CallSite CS, Function *Jump) {
    CallTable[CS] = Jump;
  }

  Trampoline *getJumpTrampoline(Function *Target) {
    auto it = JumpTable.find(Target);
    if (it == JumpTable.end())
      return nullptr;
    return it->second;
  }

  int getJumpTrampolineIndex(Function *Target) {
    auto it = JumpTable.find(Target);
    if (it == JumpTable.end())
      return -1;
    return it - JumpTable.begin();
  }

  /// Gets the tables.
  const JumpTableMap &getJumpTable() const { return JumpTable; }
  const CallTableMap &getCallTable() const { return CallTable; }

private:
  JumpTableMap JumpTable;
  CallTableMap CallTable;
};
}

#endif /* LLVM_ANALYSIS_POINTERPROTECTIONINFO_H */
