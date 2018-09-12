//===----- MarkVTablesIR.cpp - VTable Marking support pass ----------------===//
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
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "vtable-marking"

namespace {
class MarkVTablesIR : public ModulePass {
private:
  ValueMap<Constant *, MDNode *> MethodPtrMap;

public:
  static char ID; // Pass identification, replacement for typeid
  MarkVTablesIR() : ModulePass(ID) {
    initializeMarkVTablesIRPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }
};
}


char MarkVTablesIR::ID = 0;
char& llvm::MarkVTablesIRID = MarkVTablesIR::ID;

INITIALIZE_PASS(MarkVTablesIR, "mark-vtables-ir",
                "Mark VTable uses", false, false)

bool MarkVTablesIR::runOnModule(Module &M) {
  DEBUG(dbgs() << "During MarkVTablesIR pass\n");

  NamedMDNode *MethodPtrInfos = M.getNamedMetadata("llvm.trap.methodptrs");

  if (!MethodPtrInfos)
    return false;

  for (auto MD : MethodPtrInfos->operands()) {
    ValueAsMetadata *V = cast<ValueAsMetadata>(MD->getOperand(2));
    Constant *MethodStruct = cast<Constant>(V->getValue());
    MethodPtrMap[MethodStruct] = MD;
  }

  for (auto &Fn : M) {
    for (auto &BB : Fn) {
      for (auto &I : BB) {
        for (Value *Op : I.operand_values()) {
          if (Constant *C = dyn_cast<Constant>(Op)) {
            auto MethodPtrMD = MethodPtrMap.find(C);
            if (MethodPtrMD != MethodPtrMap.end()) {
              // I.setTrapInfo(TrapInfo(MethodPtrMD->second));
              DEBUG(dbgs() << "Found and marked use of method pointer struct: ");
              DEBUG(I.dump());
            }
          }
        }
      }
    }
  }

  return false;
}
