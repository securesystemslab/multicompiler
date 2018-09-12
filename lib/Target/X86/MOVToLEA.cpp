//===- MOVToLEA.cpp - Insert NOPs between instructions    ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the MOVToLEA pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "mov-to-lea"
#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/MultiCompiler/MultiCompilerOptions.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

STATISTIC(PreMOVtoLEAInstructionCount, "multicompiler: Pre-MOV to LEA instruction count");
STATISTIC(MOVCandidates,               "multicompiler: Number of MOV candidates");
STATISTIC(ReplacedMOV,                 "multicompiler: Number of substituted MOV instructions");


static cl::opt<unsigned long long>
  Seed("MOVToLEA-random-seed", cl::value_desc("seed"),
       cl::desc("Random seed for MOVToLEA"), cl::init(0));

namespace {
class MOVToLEAPass : public MachineFunctionPass {
  static char ID;

  // RNG instance for this pass
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  MOVToLEAPass() : MachineFunctionPass(ID) {}

  virtual bool runOnMachineFunction(MachineFunction &MF);

  virtual const char *getPassName() const {
    return "MOV to LEA transformation pass";
  }

};
}

char MOVToLEAPass::ID = 0;

bool MOVToLEAPass::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();

  if(Seed != 0)
     RNG.reset(Fn.getFunction()->getParent()->createRNG(Seed, this, Fn.getFunction()->getName()));
   else
     if(!RNG)
       RNG.reset(Fn.getFunction()->getParent()->createRNG(this));

  bool Changed = false;
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB)
    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
      ++PreMOVtoLEAInstructionCount;
      if (I->getNumOperands() != 2 ||
          !I->getOperand(0).isReg() || !I->getOperand(1).isReg()) {
        ++I;
        continue;
      }

      unsigned leaOpc;
      if (I->getOpcode() == X86::MOV32rr) {
        leaOpc = X86::LEA32r;
      } else if (I->getOpcode() == X86::MOV64rr) {
        leaOpc = X86::LEA64r;
      } else {
        ++I;
        continue;
      }

      unsigned int Roll = RNG->Random(100);
      ++MOVCandidates;
      if (Roll >= multicompiler::MOVToLEAPercentage) {
        ++I;
        continue;
      }

      ++ReplacedMOV;
      MachineBasicBlock::iterator J = I;
      ++I;
      addRegOffset(BuildMI(*BB, J, J->getDebugLoc(),
                           TII->get(leaOpc), J->getOperand(0).getReg()),
                   J->getOperand(1).getReg(), false, 0);
      J->eraseFromParent();
      Changed = true;
    }
  return Changed;
}

FunctionPass *llvm::createMOVToLEAPass() {
  return new MOVToLEAPass();
}
