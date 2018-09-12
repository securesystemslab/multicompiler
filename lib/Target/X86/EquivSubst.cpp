//===- EquivSubst.cpp - Insert NOPs between instructions    ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the EquivSubst pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "equiv-subst"
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

STATISTIC(PreEquivSubstInstructionCount, "multicompiler: Pre-equivalent substitution instruction count");
STATISTIC(EquivSubstCandidates,          "multicompiler: Number of equivalent substitution candidates");
STATISTIC(EquivSubstituted,              "multicompiler: Number of substituted equivalent instructions");

namespace {

class EquivInsnFilter {
public:
  virtual bool check(MachineBasicBlock &BB, const MachineInstr &MI) const = 0;
  virtual void subst(MachineBasicBlock &BB, const TargetInstrInfo *TII,
                     MachineBasicBlock::iterator I) const = 0;
};

class OpcodeRevFilter : public EquivInsnFilter {
  int Opc1, Opc2;
public:
  OpcodeRevFilter(int opc1, int opc2) : Opc1(opc1), Opc2(opc2) { }

  virtual bool check(MachineBasicBlock &BB, const MachineInstr &MI) const {
    int opc = MI.getOpcode();
    return opc == Opc1 || opc == Opc2;
  }

  virtual void subst(MachineBasicBlock &BB, const TargetInstrInfo *TII,
                     MachineBasicBlock::iterator I) const {
    int newOpc = (I->getOpcode() == Opc1) ? Opc2 : Opc1;
    I->setDesc(TII->get(newOpc));
  }
};

class MOVToLEAFilter : public EquivInsnFilter {
  int Opc1, Opc2;
public:
  MOVToLEAFilter(int opc1, int opc2) : Opc1(opc1), Opc2(opc2) { }

  virtual bool check(MachineBasicBlock &BB, const MachineInstr &MI) const {
    return MI.getNumOperands() == 2 &&
           MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() &&
           MI.getOpcode() == Opc1;
  }

  virtual void subst(MachineBasicBlock &BB, const TargetInstrInfo *TII,
                     MachineBasicBlock::iterator I) const {
    addRegOffset(BuildMI(BB, I, I->getDebugLoc(),
                         TII->get(Opc2), I->getOperand(0).getReg()),
                 I->getOperand(1).getReg(), false, 0);
    I->eraseFromParent();
  }
};

class ZeroRegFilter : public EquivInsnFilter {
  int Opc1, Opc2;
public:
  ZeroRegFilter(int opc1, int opc2) : Opc1(opc1), Opc2(opc2) { }

  virtual bool check(MachineBasicBlock &BB, const MachineInstr &MI) const {
    return MI.getNumOperands() >= 1 && MI.getOpcode() == Opc1;
  }

  virtual void subst(MachineBasicBlock &BB, const TargetInstrInfo *TII,
                     MachineBasicBlock::iterator I) const {
    unsigned reg32 = getX86SubSuperRegister(I->getOperand(0).getReg(),
                                            MVT::i32);
    BuildMI(BB, I, I->getDebugLoc(), TII->get(Opc2), reg32)
      .addReg(reg32, RegState::Kill)
      .addReg(reg32, RegState::Kill);
    I->eraseFromParent();
  }
};

                // ADD
OpcodeRevFilter ADD8RevFilter( X86::ADD8rr,  X86::ADD8rr_REV),
                ADD16RevFilter(X86::ADD16rr, X86::ADD16rr_REV),
                ADD32RevFilter(X86::ADD32rr, X86::ADD32rr_REV),
                ADD64RevFilter(X86::ADD64rr, X86::ADD64rr_REV),
                // SUB
                SUB8RevFilter( X86::SUB8rr,  X86::SUB8rr_REV),
                SUB16RevFilter(X86::SUB16rr, X86::SUB16rr_REV),
                SUB32RevFilter(X86::SUB32rr, X86::SUB32rr_REV),
                SUB64RevFilter(X86::SUB64rr, X86::SUB64rr_REV),
                // ADC
                ADC8RevFilter( X86::ADC8rr,  X86::ADC8rr_REV),
                ADC16RevFilter(X86::ADC16rr, X86::ADC16rr_REV),
                ADC32RevFilter(X86::ADC32rr, X86::ADC32rr_REV),
                ADC64RevFilter(X86::ADC64rr, X86::ADC64rr_REV),
                // SBB
                SBB8RevFilter( X86::SBB8rr,  X86::SBB8rr_REV),
                SBB16RevFilter(X86::SBB16rr, X86::SBB16rr_REV),
                SBB32RevFilter(X86::SBB32rr, X86::SBB32rr_REV),
                SBB64RevFilter(X86::SBB64rr, X86::SBB64rr_REV),
                // AND
                AND8RevFilter( X86::AND8rr,  X86::AND8rr_REV),
                AND16RevFilter(X86::AND16rr, X86::AND16rr_REV),
                AND32RevFilter(X86::AND32rr, X86::AND32rr_REV),
                AND64RevFilter(X86::AND64rr, X86::AND64rr_REV),
                // OR
                OR8RevFilter(  X86::OR8rr,   X86::OR8rr_REV),
                OR16RevFilter( X86::OR16rr,  X86::OR16rr_REV),
                OR32RevFilter( X86::OR32rr,  X86::OR32rr_REV),
                OR64RevFilter( X86::OR64rr,  X86::OR64rr_REV),
                // XOR
                XOR8RevFilter( X86::XOR8rr,  X86::XOR8rr_REV),
                XOR16RevFilter(X86::XOR16rr, X86::XOR16rr_REV),
                XOR32RevFilter(X86::XOR32rr, X86::XOR32rr_REV),
                XOR64RevFilter(X86::XOR64rr, X86::XOR64rr_REV),
                // MOV
                MOV8RevFilter( X86::MOV8rr,  X86::MOV8rr_REV),
                MOV16RevFilter(X86::MOV16rr, X86::MOV16rr_REV),
                MOV32RevFilter(X86::MOV32rr, X86::MOV32rr_REV),
                MOV64RevFilter(X86::MOV64rr, X86::MOV64rr_REV);


MOVToLEAFilter MOVToLEA32Filter(X86::MOV32rr, X86::LEA32r),
               MOVToLEA64Filter(X86::MOV64rr, X86::LEA64r);

ZeroRegFilter ZeroXOR32Filter(   X86::MOV32r0, X86::XOR32rr),
              ZeroXOR32RevFilter(X86::MOV32r0, X86::XOR32rr_REV),
              ZeroSUB32Filter(   X86::MOV32r0, X86::SUB32rr),
              ZeroSUB32RevFilter(X86::MOV32r0, X86::SUB32rr_REV);


const EquivInsnFilter *Filters[] = {
  &ADD8RevFilter, &ADD16RevFilter, &ADD32RevFilter, &ADD64RevFilter,
  &SUB8RevFilter, &SUB16RevFilter, &SUB32RevFilter, &SUB64RevFilter,
  &ADC8RevFilter, &ADC16RevFilter, &ADC32RevFilter, &ADC64RevFilter,
  &SBB8RevFilter, &SBB16RevFilter, &SBB32RevFilter, &SBB64RevFilter,
  &AND8RevFilter, &AND16RevFilter, &AND32RevFilter, &AND64RevFilter,
  &OR8RevFilter,  &OR16RevFilter,  &OR32RevFilter,  &OR64RevFilter,
  &XOR8RevFilter, &XOR16RevFilter, &XOR32RevFilter, &XOR64RevFilter,
  &MOV8RevFilter, &MOV16RevFilter, &MOV32RevFilter, &MOV64RevFilter,
  &MOVToLEA32Filter, &MOVToLEA64Filter,
  &ZeroXOR32Filter, &ZeroXOR32RevFilter, &ZeroSUB32Filter, &ZeroSUB32RevFilter,
};

class EquivSubstPass : public MachineFunctionPass {
  static char ID;

  // RNG instance for this pass
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  EquivSubstPass() : MachineFunctionPass(ID) {}

  virtual bool runOnMachineFunction(MachineFunction &MF);

  virtual const char *getPassName() const {
    return "Equivalent instruction substitution pass";
  }

};
}

char EquivSubstPass::ID = 0;

bool EquivSubstPass::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();

  if (!RNG)
    RNG.reset(Fn.getFunction()->getParent()->createRNG(this));

  bool Changed = false;
  std::vector<const EquivInsnFilter*> Candidates;
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB)
    for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
      ++PreEquivSubstInstructionCount;
      Candidates.clear();
      for (size_t i = 0; i < array_lengthof(Filters); i++)
        if (Filters[i]->check(*BB, *I))
         Candidates.push_back(Filters[i]);
      if (Candidates.empty()) {
        ++I;
        continue;
      }

      unsigned int Roll = RNG->Random(100);
      ++EquivSubstCandidates;
      if (Roll >= multicompiler::EquivSubstPercentage) {
        ++I;
        continue;
      }

      unsigned int Pick = RNG->Random(Candidates.size());
      MachineBasicBlock::iterator J = I;
      ++I;
      Candidates[Pick]->subst(*BB, TII, J);
      Changed = true;
      ++EquivSubstituted;
    }
  return Changed;
}

FunctionPass *llvm::createEquivSubstPass() {
  return new EquivSubstPass();
}


