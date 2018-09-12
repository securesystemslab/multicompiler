//===- NOPInsertion.cpp - Insert NOPs between instructions    ---*- C++ -*-===//
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
#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/MultiCompiler/MultiCompilerOptions.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"

#include <cstdio>

using namespace llvm;
using namespace multicompiler;

// namespace {
//   static cl::opt<unsigned>
//   NOPInsertionPercentage(
//     "nop-insertion-percentage",
//     cl::desc("Percentage of instructions that have NOPs prepended"),
//     cl::init(50));

//   static cl::opt<unsigned>
//   MaxNOPsPerInstruction(
//     "max-nops-per-instruction",
//     llvm::cl::desc("Maximum number of NOPs per instruction"),
//     llvm::cl::init(1));
// }

static cl::opt<unsigned long long>
Seed("NOP-random-seed", cl::value_desc("seed"),
    cl::desc("Random seed for NOP insertion"), cl::init(0));


STATISTIC(PreNOPFunctionCount,     "Pre-NOP insertion function count");
STATISTIC(PreNOPBasicBlockCount,   "Pre-NOP insertion basic block count");
STATISTIC(PreNOPInstructionCount,  "Pre-NOP insertion instruction count");
STATISTIC(InsertedInstructions,    "Total number of inserted instructions");
STATISTIC(NumNOPInstructions,      "Number of inserted NOP instructions");
STATISTIC(NumMovEBPInstructions,   "Number of inserted MOV EBP, EBP instructions");
STATISTIC(NumMovESPInstructions,   "Number of inserted MOV ESP, ESP instructions");
STATISTIC(NumLeaESIInstructions,   "Number of inserted LEA ESI, ESI instructions");
STATISTIC(NumLeaEDIInstructions,   "Number of inserted LEA EDI, EDI instructions");

namespace {
class NOPInsertionPass : public MachineFunctionPass {

  static char ID;

  bool is64Bit;

  // RNG instance for this pass
  std::unique_ptr<RandomNumberGenerator> RNG;

  void IncrementCounters(int const code);
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
       /*NOP2, NOP3, NOP4, NOP5, NOP6,*/
       MOV_EBP, MOV_ESP,
       LEA_ESI, LEA_EDI, 
       MAX_NOPS };

static const unsigned nopRegs[MAX_NOPS][2] = {
    { 0, 0 },
/*    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
*/
    { X86::EBP, X86::RBP },
    { X86::ESP, X86::RSP },
    { X86::ESI, X86::RSI },
    { X86::EDI, X86::RDI },
};

void NOPInsertionPass::IncrementCounters(int const code) {
  ++InsertedInstructions;
  switch(code) {
  case NOP:      ++NumNOPInstructions; break;
/*  case NOP2:     ++NumNOPInstructions; break;
  case NOP3:     ++NumNOPInstructions; break;
  case NOP4:     ++NumNOPInstructions; break;
  case NOP5:     ++NumNOPInstructions; break;
  case NOP6:     ++NumNOPInstructions; break;
*/
  case MOV_EBP:  ++NumMovEBPInstructions; break;
  case MOV_ESP:  ++NumMovESPInstructions; break;
  case LEA_ESI:  ++NumLeaESIInstructions; break;
  case LEA_EDI:  ++NumLeaEDIInstructions; break;
  }
}

bool NOPInsertionPass::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();

  //if (!RNG)
    //RNG.reset(Fn.getFunction()->getParent()->createRNG(this));

  if(Seed != 0)
    RNG.reset(Fn.getFunction()->getParent()->createRNG(Seed, this, Fn.getFunction()->getName()));
  else
    if(!RNG)
      RNG.reset(Fn.getFunction()->getParent()->createRNG(this));

  PreNOPFunctionCount++;
  unsigned int NOPsInserted = 0;
  int FnProb = multicompiler::getFunctionOption(
      NOPInsertionPercentage, *Fn.getFunction());
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB) {
    PreNOPBasicBlockCount++;
    PreNOPInstructionCount += BB->size();
    int BBProb = FnProb;
    if (BB->getBasicBlock()) {
      BBProb = BB->getBasicBlock()->getNOPInsertionPercentage();
      if (BBProb == multicompiler::NOPInsertionUnknown)
        BBProb = FnProb;
    }
    //printf("BB(%p):%d\n", &*BB, BBProb);
    if (BBProb <= 0)
      continue;

    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
      MachineBasicBlock::iterator J = std::next(I);
      if (I->isPseudo()) {
        I = J;
        continue;
      }
      unsigned int NumNOPs = MaxNOPsPerInstruction;
      if (NOPsInserted < EarlyNOPThreshold)
        NumNOPs = RNG->Random(EarlyNOPMaxCount);
      for (unsigned int i = 0; i < NumNOPs; i++) {
        int Roll = RNG->Random(100);
        if (Roll >= BBProb)
          continue;

        int NOPCode = RNG->Random(MAX_NOPS);

        // TODO(ahomescu): figure out if we need to preserve kill information
        MachineInstr *NewMI = NULL;
        unsigned reg = nopRegs[NOPCode][!!is64Bit];
        switch (NOPCode) {
        case NOP:
          NewMI = BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP));
          NOPsInserted++;
          break;
/*
        case NOP2:
          NewMI = BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP2));
          break;

        case NOP3:
          NewMI = addDirectMem(BuildMI(*BB, I, I->getDebugLoc(),
                                       TII->get(X86::NOOP3)), X86::RAX);
          break;

        case NOP4:
          NewMI = addRegOffset(
            BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP3)),
            X86::RAX, false, 0
            );
          break;
       
        case NOP5:
          NewMI = addRegReg(
            BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP5)),
            X86::RAX, false, X86::RAX, false		    
            );
          break;
                                        
        case NOP6:
          NewMI = addRegReg(
            BuildMI(*BB, I, I->getDebugLoc(), TII->get(X86::NOOP6)),
            X86::RAX, false, X86::RAX, false		    
            );
          break;
*/
        case MOV_EBP:
        case MOV_ESP: {
          unsigned opc = is64Bit ? X86::MOV64rr : X86::MOV32rr;
          NewMI = BuildMI(*BB, I, I->getDebugLoc(), TII->get(opc), reg)
            .addReg(reg);
          NOPsInserted++;
          break;
        }

        case LEA_ESI:
        case LEA_EDI: {
          unsigned opc = is64Bit ? X86::LEA64r : X86::LEA32r;
          NewMI = addRegOffset(BuildMI(*BB, I, I->getDebugLoc(),
                                       TII->get(opc), reg),
                               reg, false, 0);
          NOPsInserted++;
          break;
        }
        }

        if (NewMI != NULL) {
          IncrementCounters(NOPCode);
          NewMI->setFlag(MachineInstr::InsertedNOP);
        }
      }
      I = J;
    }
  }
  return true;
}

FunctionPass *llvm::createNOPInsertionPass(bool is64Bit) {
  return new NOPInsertionPass(is64Bit);
}


