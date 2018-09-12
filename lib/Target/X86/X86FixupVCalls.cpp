//===----- X86FixupVCalls.cpp - Fix up immediate width of vcalls ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "x86-fixup-vcalls"

namespace {
class FixupVCallsPass : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  FixupVCallsPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &Fn) override;

private:
  const TargetMachine *TM;
  const X86InstrInfo *TII; // Machine instruction info.
};
char FixupVCallsPass::ID = 0;
}

FunctionPass *llvm::createX86FixupVCalls() { return new FixupVCallsPass(); }

bool FixupVCallsPass::runOnMachineFunction(MachineFunction &Fn) {
  TM = &Fn.getTarget();
  TII =
      static_cast<const X86InstrInfo *>(Fn.getSubtarget().getInstrInfo());

  DEBUG(dbgs() << "During X86FixupVCalls pass\n");
  const DataLayout *DL = &Fn.getDataLayout();
  unsigned TrampSize = DL->getTypeSizeInBits(Type::getTrampolineTy(Fn.getFunction()->getContext()))/8;

  for (auto &BB : Fn) {
    for (auto &MI : BB) {
      TrapInfo TI = MI.getTrapInfo();
      if (!TI.isUnknown()) {
        uint64_t MaxNumVFuncs = TI.getMaxNumVFuncs();
        assert(MaxNumVFuncs <= (1 << 15)/TrampSize && "Must not have more than 6553 virtual functions!");
        if (MaxNumVFuncs > (1 << 7)/TrampSize) {
          DEBUG(dbgs() << MaxNumVFuncs << " vtable entries\n");
          if (MI.getOpcode() == X86::ADD64ri8) {
            DEBUG(dbgs() << "Replacing ADD64ri8 with ADD64ri16\n");
            MI.setDesc(TII->get(X86::ADD64ri32));
          } else {
            DEBUG(dbgs() << "Warning: Did not recognize vcall instruction, but I need to make sure it has room!\n");
            DEBUG(MI.dump());
          }
        }
      }
    }
  }

  return false;
}
