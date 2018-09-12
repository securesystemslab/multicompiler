//===----- MarkVTables.cpp - VTable Marking support pass ------------------===//
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

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "vtable-marking"

namespace {
class MarkVTables : public MachineFunctionPass {
private:
  /// Current set of PHIs we've seen during propagation, so that we don't visit
  /// the same PHI nodes repeatedly
  SmallPtrSet<const MachineInstr *, 16> VisitedPHIs;

  /// Current vtable info being propagated
  TrapInfo CurTrapInfo;

  /// Current function
  MachineFunction *MF;

  void MarkInstr(MachineInstr *MI);

public:
  static char ID; // Pass identification, replacement for typeid
  MarkVTables() : MachineFunctionPass(ID) {
    initializeMarkVTablesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
}


char MarkVTables::ID = 0;
char& llvm::MarkVTablesID = MarkVTables::ID;

INITIALIZE_PASS(MarkVTables, "mark-vtables",
                "Mark VTable uses", false, false)

void MarkVTables::MarkInstr(MachineInstr *MI) {
  if (MI->isSubregToReg()) {
    MachineOperand &DefOp = *(MF->getRegInfo().def_begin(MI->getOperand(2).getReg()));
    MarkInstr(DefOp.getParent());
    return;
  } else if (MI->isCopy()) {
    MachineOperand &DefOp = *(MF->getRegInfo().def_begin(MI->getOperand(1).getReg()));
    MarkInstr(DefOp.getParent());
    return;
  } else if (MI->isPHI()) {
    if (VisitedPHIs.insert(MI).second) {
      for (unsigned i = 1, e = MI->getNumOperands(); i != e; ++i) {
        MachineOperand &MO = MI->getOperand(i);
        assert(MO.isReg() && "Cannot handle non-reg virtual function pointers");
        MachineOperand &DefOp = *(MF->getRegInfo().def_begin(MO.getReg()));
        MarkInstr(DefOp.getParent());
        ++i; // skip basic block operand
      }
    }
    return;
  } else if (MI->getNumOperands() >= 3 &&
             MI->getOperand(2).isImm()) {
    // ADD x, constant
    MI->setTrapInfo(CurTrapInfo);
    return;
  } else if (MI->getNumOperands() >= 2 &&
             (MI->getOperand(1).isImm() ||
              MI->getOperand(1).isGlobal())) {
    // MOV constant
    // MOV global+offset
    MI->setTrapInfo(CurTrapInfo);
    return;
  } else if (MI->getNumOperands() >= 3 &&
             MI->getOperand(1).isReg()) {
    MachineOperand &ConstOperand = *(MF->getRegInfo().def_begin(MI->getOperand(1).getReg()));
    MarkInstr(ConstOperand.getParent());
    return;
  }
  errs() << "Could not add vtable info to: ";
  MI->dump();
}

bool MarkVTables::runOnMachineFunction(MachineFunction &Fn) {
  DEBUG(dbgs() << "During MarkVTables pass\n");

  MF = &Fn;

  for (auto &BB : Fn) {
    for (auto &MI : BB) {
      // for (MachineMemOperand *MMO : MI.memoperands()) {
      //   if (const MDNode *VTableInfo = MMO->getVTableInfo()) {
      //     DEBUG(dbgs() << "Found vtable deref, offset = " << MMO->getOffset() << "\n");
      //     DEBUG(MI.print(dbgs(), &MF->getTarget()));
      //     DEBUG(MMO->getValue()->dump());

      //     // const Value *VTablePtr = MMO->getValue();
      //     // Type *ClassType = nullptr;
      //     // if (auto Load = dyn_cast<LoadInst>(VTablePtr)) {
      //     //   ClassType = cast<BitCastInst>(Load->getOperand(0))->getSrcTy();
      //     //   ClassType = cast<PointerType>(ClassType)->getElementType();
      //     // }

      //     for (auto &D : MI.defs()) {
      //       DEBUG(dbgs() << "Def: " << D << '\n');
      //       for (auto &U : MF->getRegInfo().use_instructions(D.getReg())) {
      //         DEBUG(dbgs() << U << '\n');
      //         for (auto MO : U.operands()) {
      //           if (MO.isImm()) {
      //             U.addOperand(MachineOperand::CreateVTableInfo(VTableInfo));
      //           }
      //         }
      //       }
      //     }
      //   }
      // }

      CurTrapInfo = MI.getTrapInfo();
      if (!CurTrapInfo.isUnknown() && MI.isCall()) {
        DEBUG(dbgs() << "Found metadata in call\n");
        DEBUG(MI.dump());
        MachineInstr *LoadInstr = nullptr;
        MachineOperand &DefOp = *(MF->getRegInfo().def_begin(MI.getOperand(0).getReg()));
        LoadInstr = DefOp.getParent();

        MI.setTrapInfo(TrapInfo());

        VisitedPHIs.clear();
        MarkInstr(LoadInstr);
      }
    }
  }

  return false;
}
