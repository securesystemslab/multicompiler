//===- StackToHeapPromotion.cpp - StackToHeap Promotion ---------*- C++ -*-===//
//
//  This is a multicompiler transformation that randomly promotes
//  a stack buffer to heap allocation.
//
//  Author: Yeoul Na
//  Date: Dec 7, 2016
//
//===----------------------------------------------------------------------===//


#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/MultiCompiler/MultiCompilerOptions.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/RandomNumberGenerator.h"

using namespace llvm;

#define DEBUG_TYPE "stack-to-heap-promotion"

static cl::opt<unsigned long long>
Seed("stack-to-heap-promotion-random-seed", cl::value_desc("seed"),
     cl::desc("Random seed for stack-to-heap promotion"), cl::init(0));

// FIXME: Stack-to-heap promotion emits malloc() and free() which are
// async-signal-unsafe functions. These functions should not be invoked
// inside signal handlers to avoid undefined behavior. Currently, we
// simply whitelists the known signal handlers in our ATDs. However, it
// should(and can) be done in a more general and elegant way in the future.
auto SigHandlerList = {
#define WHITELIST(X) StringRef(#X),
#include "ATDSigHandlers.def"
};

StringSet<> SigHandlerSet(SigHandlerList);

namespace {
class StackToHeapPromotion : public FunctionPass {
  const TargetMachine *TM;
  const DataLayout *DL;
  std::unique_ptr<RandomNumberGenerator> RNG;

  Type *IntPtrTy;

  //Find all instruction for unsafestack
  void findInsts(Function &F, 
                 SmallVectorImpl<AllocaInst *> &StaticAllocas,
                 SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                 SmallVectorImpl<Argument *> &ByValArguments,
                 SmallVectorImpl<ReturnInst *> &Returns);

  /// \brief Calculate the allocation size of a given alloca. Returns 0 if the
  /// size can not be statically determined.
  uint64_t getStaticAllocaAllocationSize(const AllocaInst* AI);

public:
  static char ID;

  StackToHeapPromotion(const TargetMachine *TM)
      : FunctionPass(ID), TM(TM) {
    initializeStackToHeapPromotionPass(*PassRegistry::getPassRegistry());
  }

  StackToHeapPromotion() : StackToHeapPromotion(nullptr) {}

  bool doInitialization(Module &M) override {
    DL = &M.getDataLayout();

    IntPtrTy = DL->getIntPtrType(M.getContext());

    return false;
  }

  bool runOnFunction(Function& F) override;
};

uint64_t StackToHeapPromotion::getStaticAllocaAllocationSize(const AllocaInst* AI) {
  uint64_t Size = DL->getTypeAllocSize(AI->getAllocatedType());
  if (AI->isArrayAllocation()) {
    auto C = dyn_cast<ConstantInt>(AI->getArraySize());
    if (!C)
      return 0;
    Size *= C->getZExtValue();
  }
  return Size;
}

void StackToHeapPromotion::findInsts(Function &F,
                                 SmallVectorImpl<AllocaInst *> &StaticAllocas,
                                 SmallVectorImpl<AllocaInst *> &DynamicAllocas,
                                 SmallVectorImpl<Argument *> &ByValArguments,
                                 SmallVectorImpl<ReturnInst *> &Returns) {
  for (Instruction &I : instructions(&F)) {
    if (auto AI = dyn_cast<AllocaInst>(&I)) {
		  if (AI->isStaticAlloca())
        StaticAllocas.push_back(AI);
      else
        DynamicAllocas.push_back(AI);
    } else if (auto II = dyn_cast<IntrinsicInst>(&I)) {
      if (II->getIntrinsicID() == Intrinsic::gcroot)
        llvm::report_fatal_error(
            "gcroot intrinsic not compatible with safestack attribute");
    } else if (auto RI = dyn_cast<ReturnInst>(&I)) {
      Returns.push_back(RI);
    } 
  }
  for (Argument &Arg : F.args()) {
    if (!Arg.hasByValAttr())
      continue;

    ByValArguments.push_back(&Arg);
  }
}


bool StackToHeapPromotion::runOnFunction(Function &F) {

  if (!multicompiler::getFunctionOption(multicompiler::StackToHeapPromotion, F))
    return false;

  // Do not apply stack-to-heap promotion to functions known to be signal handlers.
  if (SigHandlerSet.find(F.getName()) != SigHandlerSet.end()) {
    DEBUG(errs() << "Whitelist a signal handler " << F.getName() << "\n");
    return false;
  }
  //Set up random number generater
  if (Seed != 0)
    RNG.reset(F.getParent()->createRNG(Seed, this, F.getName()));
  else if (!RNG)
    RNG.reset(F.getParent()->createRNG(this));

  SmallVector<AllocaInst *, 16> StaticAllocas;
  SmallVector<AllocaInst *, 16> DynamicAllocas;
  SmallVector<Argument *, 4> ByValArguments;
  SmallVector<ReturnInst *, 4> Returns;
  SmallVector<Instruction *, 4> DynamicStacks;
 
  findInsts(F, StaticAllocas, DynamicAllocas, ByValArguments, Returns);

  IRBuilder<> IRB(&F.front(), F.begin()->getFirstInsertionPt());

  for (Argument *Arg : ByValArguments) {
    unsigned nonce = RNG->Random(100);
    if(nonce >= multicompiler::getFunctionOption(multicompiler::StackToHeapPercentage, F))
      continue; 

    Type *Ty = Arg->getType()->getPointerElementType();
    uint64_t Size = DL->getTypeStoreSize(Ty);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    Instruction *CI = CallInst::CreateMalloc(&*F.begin()->getFirstInsertionPt(), IntPtrTy, Ty, 
                                             ConstantInt::get(IntPtrTy, Size), ConstantInt::get(IntPtrTy, 1), 
                                             nullptr, Twine(""));
    DynamicStacks.push_back(CI);
    Arg->replaceAllUsesWith(CI);
    IRB.SetInsertPoint(CI->getNextNode());
    IRB.CreateMemCpy(CI, Arg, Size, Arg->getParamAlignment());
  }

  for (AllocaInst *AI : StaticAllocas) {
    unsigned nonce = RNG->Random(100);
    if(nonce >= multicompiler::getFunctionOption(multicompiler::StackToHeapPercentage, F))
      continue; 

    uint64_t Size = getStaticAllocaAllocationSize(AI);
    if (Size == 0)
      Size = 1; // Don't create zero-sized stack objects.

    IRB.SetInsertPoint(AI);
    Type *Ty = AI->getAllocatedType();

    Instruction *CI = CallInst::CreateMalloc(AI, IntPtrTy, Ty, ConstantInt::get(IntPtrTy, Size));
    DynamicStacks.push_back(CI);
    AI->replaceAllUsesWith(CI);
    AI->eraseFromParent();
  }

  // Promoting dynamic allocas to the heap requires that we dynamically allocate
  // a pointer value to store the heap allocation's address. To avoid memory
  // leaks we need to locate and free all of these dynamic stored address at
  // restore_stacks and returns. However, this is tricky since we don't know how
  // many and which slots are dynamic heap addresses that need to be freed. ASAN
  // does something like this, but needs a special intrinsic
  // (llvm.get.dynamic.area.offset) to find the end of the dynamic allocation
  // area. For now, let's just not handle dynamic allocas. We can improve this
  // later if necessary.  Idea: we can store the dynamic heap addresses in a
  // linked list structure constructed via dynamic allocas.

  for (ReturnInst *RI : Returns) {
    for (Instruction *DS : DynamicStacks)
      CallInst::CreateFree(DS, RI);
  }

  return true;
}

} // End of namespace

char StackToHeapPromotion::ID = 0;
INITIALIZE_TM_PASS_BEGIN(StackToHeapPromotion, "stack-to-heap-promot",
                         "Pass for randomly promoting buffers to heap", false, false) 
INITIALIZE_TM_PASS_END(StackToHeapPromotion, "stack-to-heap-promot",
                       "Pass for randomly promoting buffers to heap", false, false) 

FunctionPass *llvm::createStackToHeapPromotionPass(const llvm::TargetMachine *TM) {
    return new StackToHeapPromotion(TM);
}
