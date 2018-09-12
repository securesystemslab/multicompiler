//===-- CookieSetter.cpp - Cookie Setter for destination verification --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License (yet).
//
// Copyright 2015 Immunant, Inc.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86RegisterInfo.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "cookie-protection"

namespace {
  class CookieSetter : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo *MRI;

    ValueMap<const Function*, uint64_t> FunctionCookies;

    void LowerSetCookieInstrs(MachineFunction &MF);

    unsigned ClearRegister(MachineBasicBlock &BB, MachineBasicBlock::iterator I,
                           unsigned Reg);

  public:
    static char ID; // Pass identification
    CookieSetter() : MachineFunctionPass(ID) {
      initializeCookieSetterPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;
  };
}

char CookieSetter::ID = 0;
INITIALIZE_PASS(CookieSetter, "cookie-inserter",
                "Cookie Setter", false, false)

FunctionPass *llvm::createCookieSetterPass() {
  return new CookieSetter();
}

void CookieSetter::LowerSetCookieInstrs(MachineFunction &MF) {
  for (auto &BB : MF) {
    bool CookieSet = false;
    uint64_t CookieValue;
    for (auto MBBI = BB.begin(), MBBE = BB.end(); MBBI != MBBE; ) {
      MachineInstr *MI = MBBI++;
      if (MI->getOpcode() == X86::SET_COOKIE) {
        // Set cookie

        // FIXME? should this move the instruction to directly before the call
        // or ret so that R11 cannot get clobbered?
        DEBUG(dbgs() << "Setting cookie in BB " << BB.getName() << "\n");
        CookieSet = true;
        CookieValue = MI->getOperand(0).getImm();
        MI->eraseFromParent();
      } else if (CookieSet && MI->isCall()) {
        DEBUG(dbgs() << "Adding R11 implicit use to call in BB " << BB.getName() << "\n");
        BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
                TII->get(X86::MOV64ri), X86::R11)
          .addImm(CookieValue);
        MachineInstrBuilder(MF, MI).addReg(X86::R11, RegState::Implicit);
        CookieSet = false;
      } else if (CookieSet && MI->isReturn()) {
        BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
                TII->get(X86::MOV64ri), X86::R11)
          .addImm(CookieValue);
        CookieSet = false;
      }
    }
    if (CookieSet) {
      // We're at the end of a basic block, but haven't emitted a saved
      // cookie. Emit it now, since the call must be in the next block. This is
      // somewhat suboptimal, but I'm not gonna worry about it now.
      auto I = BB.getFirstTerminator();
      DebugLoc DL;
      if (I != BB.end())
        DL = I->getDebugLoc();
      BuildMI(BB, I, DL, TII->get(X86::MOV64ri), X86::R11)
        .addImm(CookieValue);
    }
  }
}

unsigned CookieSetter::ClearRegister(MachineBasicBlock &BB, MachineBasicBlock::iterator I,
                                       unsigned Reg) {
  DebugLoc DL;
  unsigned TmpReg =
    MRI->createVirtualRegister(&X86::GR64RegClass);
  BuildMI(BB, I, DL, TII->get(X86::XOR64rr), TmpReg)
    .addReg(Reg, RegState::Kill).addReg(Reg, RegState::Kill);
  return TmpReg;
}  

bool CookieSetter::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  if (!MF.getFunction()->hasFnAttribute(Attribute::CookieCheck)) {
    DEBUG(dbgs() << "Setting cookies for " << MF.getName() << "\n");
    LowerSetCookieInstrs(MF);
  }

  return true;
}
