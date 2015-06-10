//===- ShuffleFunctions.cpp - Randomly shuffle list of functions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass randomly shuffles the functions inside a Module.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar.h"

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"
using namespace llvm;

#define DEBUG_TYPE "shufflefunctions"

namespace {
  struct ShuffleFunctionsPass : public ModulePass {
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
    static char ID; // Pass identification, replacement for typeid
    ShuffleFunctionsPass() : ModulePass(ID) {
      initializeShuffleFunctionsPassPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module& M) override;
  };
}

// FIXME: Rename the pass to "shuffle-functions" if we ever get rid of
// the command line flag in CommandFlags that has the same name.
char ShuffleFunctionsPass::ID = 0;
INITIALIZE_PASS(ShuffleFunctionsPass, "shuffle-functions-pass",
                "Shuffle Functions", false, false)

ModulePass* llvm::createShuffleFunctionsPass() { return new ShuffleFunctionsPass(); }

bool ShuffleFunctionsPass::runOnModule(Module& M) {
  auto &Funcs = M.getFunctionList();
  SmallVector<Function*, 16> Shuffled;
  for (auto I = Funcs.begin(); I != Funcs.end(); ) {
    auto Func = Funcs.remove(I);
    Shuffled.push_back(Func);
  }

  auto RNG = M.createRNG(this);
  std::shuffle(Shuffled.begin(), Shuffled.end(), *RNG);
  Funcs.insert(Funcs.begin(), Shuffled.begin(), Shuffled.end());
  return true; // FIXME: should return false instead???
}

