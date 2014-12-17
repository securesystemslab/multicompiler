//===-- NoopInsertion.h - Noop Insertion -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds fine-grained diversity by displacing code using randomly
// placed (optionally target supplied) Noop instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_NOOPINSERTION_H
#define LLVM_CODEGEN_NOOPINSERTION_H

#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class NoopInsertion : public MachineFunctionPass {
public:
  static char ID;

  NoopInsertion();

  bool runOnMachineFunction(MachineFunction &MF);

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

}

#endif // LLVM_CODEGEN_NOOPINSERTION_H
