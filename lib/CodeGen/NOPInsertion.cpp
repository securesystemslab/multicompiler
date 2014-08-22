//===- NOPInsertion.cpp - NOP Insertion -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds fine-grained diversity by displacing code using randomly
// placed (optionally target supplied) NOP instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/NOPInsertion.h"
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

#define DEBUG_TYPE "nop-insertion"

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

char NOPInsertion::ID = 0;
char &llvm::NOPInsertionID = NOPInsertion::ID;
INITIALIZE_PASS(NOPInsertion, "nop-insertion",
                "NOP Insertion for fine-grained code randomization", false,
                false)

NOPInsertion::NOPInsertion() : MachineFunctionPass(ID) {
  initializeNOPInsertionPass(*PassRegistry::getPassRegistry());

  // clamp percentage to 100
  if (NOPInsertionPercentage > 100)
    NOPInsertionPercentage = 100;
}

void NOPInsertion::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool NOPInsertion::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getTarget().getInstrInfo();

  RandomNumberGenerator *RNG = Fn.getFunction()->getParent()->createRNG(this);
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
        unsigned Roll = (*RNG)() % 100; // FIXME: not uniform
        if (Roll >= NOPInsertionPercentage)
          continue;

        TII->insertNoop(*BB, I, RNG);

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

