//===- NoopInsertion.cpp - Noop Insertion -----------------------------------===//
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

#include "llvm/CodeGen/NoopInsertion.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetInstrInfo.h"
using namespace llvm;

#define DEBUG_TYPE "noop-insertion"

static cl::opt<unsigned>
NoopInsertionPercentage(
  "noop-insertion-percentage",
  cl::desc("Percentage of instructions that have Noops prepended"),
  cl::init(50));

static cl::opt<unsigned>
MaxNoopsPerInstruction(
  "max-noops-per-instruction",
  llvm::cl::desc("Maximum number of Noops per instruction"),
  llvm::cl::init(1));


STATISTIC(InsertedNoops,
          "Total number of noop type instructions inserted for diversity");

char NoopInsertion::ID = 0;
char &llvm::NoopInsertionID = NoopInsertion::ID;
INITIALIZE_PASS(NoopInsertion, "noop-insertion",
                "Noop Insertion for fine-grained code randomization", false,
                false)

NoopInsertion::NoopInsertion() : MachineFunctionPass(ID) {
  initializeNoopInsertionPass(*PassRegistry::getPassRegistry());

  // clamp percentage to 100
  if (NoopInsertionPercentage > 100)
    NoopInsertionPercentage = 100;
}

void NoopInsertion::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool NoopInsertion::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();

  RandomNumberGenerator *RNG = Fn.getFunction()->getParent()->createRNG(this);
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB) {
    MachineBasicBlock::iterator FirstTerm = BB->getFirstTerminator();
    // Insert Noops before instruction.
    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
      MachineBasicBlock::iterator NextI = std::next(I);
      if (I->isPseudo()) {
        I = NextI;
        continue;
      }

      // Insert random number of Noop-like instructions.
      for (unsigned i = 0; i < MaxNoopsPerInstruction; i++) {
        unsigned Roll = (*RNG)() % 100; // FIXME: not uniform
        if (Roll >= NoopInsertionPercentage)
          continue;

        TII->insertNoop(*BB, I, RNG);

        ++InsertedNoops;
      }

      // Do not insert Noops between terminators.
      if (I == FirstTerm)
        break;

      I = NextI;
    }
  }
  return true;
}
