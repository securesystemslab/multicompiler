//===----- GlobalRandomization.cpp - Global Variable Randomization --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements random reordering and padding of global variables.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "multicompiler"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MultiCompiler/MultiCompilerOptions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
//                           GlobalRandomization Pass
//===----------------------------------------------------------------------===//


namespace {

static cl::opt<unsigned long long>
Seed("global-randomization-random-seed", cl::value_desc("seed"),
     cl::desc("Random seed for global padding and shuffling"), cl::init(0));

class GlobalRandomization : public ModulePass {
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  virtual bool runOnModule(Module &M);

public:
  static char ID; // Pass identification, replacement for typeid.
  GlobalRandomization() : ModulePass(ID) {
    initializeGlobalRandomizationPass(*PassRegistry::getPassRegistry());
  }

private:
  GlobalVariable* CreatePadding(GlobalVariable::LinkageTypes linkage,
                                GlobalVariable *G = nullptr);

  Module *CurModule;
};
}

char GlobalRandomization::ID = 0;
INITIALIZE_PASS(GlobalRandomization, "global-randomization",
                "Global Randomization pass", false, false)

ModulePass *llvm::createGlobalRandomizationPass() {
  return new GlobalRandomization();
}

static int compareNames(Constant *const *A, Constant *const *B) {
  return (*A)->getName().compare((*B)->getName());
}

static void setUsedInitializer(GlobalVariable *V, Module &M,
                               const SmallPtrSet<GlobalValue *, 8> &Init) {
  if (Init.empty()) {
    if (V)
      V->eraseFromParent();
    return;
  }

  // Type of pointer to the array of pointers.
  PointerType *Int8PtrTy = Type::getInt8PtrTy(M.getContext(), 0);

  SmallVector<llvm::Constant *, 8> UsedArray;
  for (GlobalValue *GV : Init) {
    Constant *Cast
      = ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, Int8PtrTy);
    UsedArray.push_back(Cast);
  }
  // Sort to get deterministic order.
  array_pod_sort(UsedArray.begin(), UsedArray.end(), compareNames);
  ArrayType *ATy = ArrayType::get(Int8PtrTy, UsedArray.size());

  if (V)
    V->removeFromParent();
  GlobalVariable *NV =
      new GlobalVariable(M, ATy, false, llvm::GlobalValue::AppendingLinkage,
                         llvm::ConstantArray::get(ATy, UsedArray), "");
  if (V)
    NV->takeName(V);
  else
    NV->setName("llvm.used");
  NV->setSection("llvm.metadata");
  if (V)
    delete V;
}

GlobalVariable* GlobalRandomization::CreatePadding(GlobalVariable::LinkageTypes linkage,
                                                   GlobalVariable *G) {
  Type *Int8Ty = Type::getInt8Ty(CurModule->getContext());

  unsigned Size = RNG->Random(multicompiler::GlobalPaddingMaxSize-1)+1;
  ArrayType *PaddingType = ArrayType::get(Int8Ty, Size);
  Constant *Init;
  if (!G || G->getInitializer()->isZeroValue()) {
    Init = ConstantAggregateZero::get(PaddingType);
  } else {
    SmallVector<Constant*, 32> PaddingInit(Size,
                                           ConstantInt::get(Int8Ty, 0xff));
    Init = ConstantArray::get(PaddingType, PaddingInit);
  }

  return new GlobalVariable(*CurModule, PaddingType, false,
                            linkage, Init, "[padding]", G);
}

template<typename T>
void reverse(SymbolTableList<T>& list) {
  if (list.empty()) return;

  // using std::reverse directly on an iplist<T> would be simpler
  // but isn't supported.
  SmallVector<T*, 10> rlist;
  for (typename SymbolTableList<T>::iterator i = list.begin();
       i != list.end();) {
    // iplist<T>::remove increments the iterator which is why
    // the for loop doesn't.
    T* t = list.remove(i);
    rlist.push_back(t);
  }

  std::reverse(rlist.begin(), rlist.end());

  for(typename SmallVector<T*, 10>::size_type i = 0; i < rlist.size(); i++){
    list.push_back(rlist[i]);
  }
}

bool GlobalRandomization::runOnModule(Module &M) {
  if (!multicompiler::ShuffleGlobals &&
      !multicompiler::ReverseGlobals &&
      multicompiler::GlobalPaddingPercentage == 0)
    return false;

  CurModule = &M;

  if (Seed != 0)
    RNG.reset(M.createRNG(Seed, this));
  else
    RNG.reset(M.createRNG(this));

  Module::GlobalListType &Globals = M.getGlobalList();

  SmallVector<GlobalVariable *, 10> WorkList;
  SmallPtrSet<GlobalValue *, 8> UsedGlobals;
  GlobalVariable *UsedV = collectUsedGlobalVariables(M, UsedGlobals,
                                                     false);

  for (GlobalVariable &G : Globals) {
    if (G.hasInitializer() && !G.isConstant())
      WorkList.push_back(&G);
  }

  unsigned long NormalGlobalCount = 0;
  unsigned long CommonGlobalCount = 0;
  for (GlobalVariable *G : WorkList) {
    GlobalVariable::LinkageTypes linkage = GlobalVariable::InternalLinkage;
    if (G->hasCommonLinkage()) {
      linkage = GlobalVariable::CommonLinkage;
      CommonGlobalCount++;
    } else {
      NormalGlobalCount++;
    }

    if (multicompiler::GlobalPaddingPercentage == 0)
      continue;

    unsigned Roll = RNG->Random(100);
    if (Roll >= multicompiler::GlobalPaddingPercentage)
      continue;

    //Insert padding
    UsedGlobals.insert(CreatePadding(linkage, G));
  }

  //Increase the number of globals to increase the entropy of their layout.
  if (NormalGlobalCount > 0)
    for (; NormalGlobalCount < multicompiler::GlobalMinCount; ++NormalGlobalCount)
      UsedGlobals.insert(CreatePadding(GlobalVariable::InternalLinkage));

  if (CommonGlobalCount > 0)
    for (; CommonGlobalCount < multicompiler::GlobalMinCount; ++CommonGlobalCount)
      UsedGlobals.insert(CreatePadding(GlobalVariable::CommonLinkage));

  setUsedInitializer(UsedV, M, UsedGlobals);

  //Global variable randomization
  if (multicompiler::ShuffleGlobals) {
    RNG->shuffle(Globals);
    DEBUG(dbgs() << "shuffled order of " << Globals.size() << " global variables\n");
  }

  //Reverse global variables
  if (multicompiler::ReverseGlobals) {
    reverse(Globals);
    DEBUG(dbgs() << "reversed order of " << Globals.size() <<" global variables\n");
  }

  //Dump globals after randomization and reversal. Note: linker may affect this order.
  DEBUG(dbgs() << "start list of randomized global variables\n");
  for (GlobalVariable &G : Globals) {
    DEBUG(G.dump());
  }
  DEBUG(dbgs() << "end list of randomized global variables\n");

  return true;
}
