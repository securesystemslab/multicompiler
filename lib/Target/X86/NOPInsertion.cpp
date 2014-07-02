//===- NOPInsertion.cpp - Insert NOPs between instructions ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the NOPInsertion pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "nop-insertion"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

static cl::opt<unsigned>
NOPInsertionPercentage(
  "nop-insertion-percentage",
  cl::desc("Percentage of instructions that have NOPs prepended"),
  cl::init(50));

static cl::opt<unsigned>
MaxNOPsPerInstruction(
  "max-nops-per-instruction",
  llvm::cl::desc("Maximum number of NOPs per instruction"),
  llvm::cl::init(1));


STATISTIC(InsertedNOPs,
          "Total number of noop type instructions inserted for diversity");

namespace {
class NOPInsertionPass : public MachineFunctionPass {

  static char ID;

  bool is64Bit;

public:
  NOPInsertionPass(bool is64Bit_) :
      MachineFunctionPass(ID), is64Bit(is64Bit_) {
  }

  virtual bool runOnMachineFunction(MachineFunction &MF);

  virtual const char *getPassName() const {
    return "NOP insertion pass";
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
}

char NOPInsertionPass::ID = 0;

enum { NOP,
       MOV_EBP, MOV_ESP,
       LEA_ESI, LEA_EDI,
       MAX_NOPS };

static const unsigned nopRegs[MAX_NOPS][2] = {
    { 0, 0 },
    { X86::EBP, X86::RBP },
    { X86::ESP, X86::RSP },
    { X86::ESI, X86::RSI },
    { X86::EDI, X86::RDI },
};

bool NOPInsertionPass::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getTarget().getInstrInfo();

  RandomNumberGenerator &RNG = Fn.getFunction()->getParent()->getRNG();
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB) {
    MachineBasicBlock::iterator FirstTerm = BB->getFirstTerminator();
    // Insert NOPs before instruction.
    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
      MachineBasicBlock::iterator NextI = std::next(I);
      if (I->isPseudo()) {
        I = NextI;
        continue;
      }
      // Insert random number of NOP-like instructions.
      for (unsigned i = 0; i < MaxNOPsPerInstruction; i++) {
        unsigned Roll = RNG.next(100);
        if (Roll >= NOPInsertionPercentage)
          continue;

        unsigned NOPCode = RNG.next(MAX_NOPS);

        MachineInstr *NewMI = NULL;
        unsigned reg = nopRegs[NOPCode][is64Bit];
        switch (NOPCode) {
        case NOP:
          NewMI = BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP));
          break;
        case MOV_EBP:
        case MOV_ESP: {
          unsigned opc = is64Bit ? X86::MOV64rr : X86::MOV32rr;
          NewMI = BuildMI(*BB, I, I->getDebugLoc(), TII->get(opc), reg)
            .addReg(reg);
          break;
        }

        case LEA_ESI:
        case LEA_EDI: {
          unsigned opc = is64Bit ? X86::LEA64r : X86::LEA32r;
          NewMI = addRegOffset(BuildMI(*BB, I, I->getDebugLoc(),
                                       TII->get(opc), reg),
                               reg, false, 0);
          break;
        }
        }

        if (NewMI != NULL)
          ++InsertedNOPs;
      }
      // Do not insert NOPs between terminators.
      if (I == FirstTerm)
        break;

      I = NextI;
    }
  }
  return true;
}

namespace llvm {
  FunctionPass *createNOPInsertionPass(bool is64Bit) {
    return new NOPInsertionPass(is64Bit);
  }
}
