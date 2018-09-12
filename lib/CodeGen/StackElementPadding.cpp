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
#include "llvm/Support/RandomNumberGenerator.h"
#define DEBUG_TYPE "stackElementPadding"

//Padding for unsafe stack is implemented in SafeStack.cpp
using namespace llvm;

namespace llvm {

STATISTIC(NumAllocas, "Total number of allocas");
}

namespace {

static cl::opt<unsigned long long>
Seed("stack-element-padding-random-seed", cl::value_desc("seed"),
     cl::desc("Random seed for stack element padding"), cl::init(0));



class StackElementPadding : public FunctionPass {
  const TargetMachine *TM;
  const TargetLoweringBase *TLI;
  const DataLayout *DL;
  std::unique_ptr<RandomNumberGenerator> RNG;

  //Find all instruction for unsafestack
  void findInsts(Function &Fn, SmallVectorImpl<AllocaInst *> &Allocas);

public:
  static char ID;

  StackElementPadding() : FunctionPass(ID) {
    llvm::initializeStackElementPaddingPass(*PassRegistry::getPassRegistry());
  }

  StackElementPadding(const TargetMachine *TM) : FunctionPass(ID), TM(TM) {
    llvm::initializeStackElementPaddingPass(*PassRegistry::getPassRegistry());
  }

  virtual bool runOnFunction(Function& Fn);
};



void StackElementPadding::findInsts(Function &Fn,
                          SmallVectorImpl<AllocaInst *> &Allocas) {

  for (inst_iterator It = inst_begin(&Fn), Ie = inst_end(&Fn); It != Ie; ++It) {
    Instruction *I = &*It;

    if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
      ++NumAllocas;

      Allocas.push_back(AI);
    }
  }
}


bool StackElementPadding::runOnFunction(Function& Fn) {

  if (multicompiler::StackElementPaddingPercentage != 0 && 
      multicompiler::MaxStackElementPadding == 0)
    return false;

  //Set up random number generater
  if (Seed != 0)
    RNG.reset(Fn.getParent()->createRNG(Seed, this, Fn.getName()));
  else
    if (!RNG)
      RNG.reset(Fn.getParent()->createRNG(this));

  SmallVector<AllocaInst*, 20> Allocas;
 
  findInsts(Fn, Allocas);  //Find all allocation instructions(Padding will be inserted right after allocated data)

  IRBuilder<> IRB(&*Fn.getEntryBlock().getFirstInsertionPt());
  for (SmallVectorImpl<AllocaInst*>::iterator It = Allocas.begin(),
                                      Ie = Allocas.end(); It != Ie; ++It)
  {
    unsigned nonce = RNG->Random(100);
    if(nonce >= multicompiler::StackElementPaddingPercentage)
      continue; //XX% probability of pad insertion

    unsigned paddingSize = RNG->Random(multicompiler::MaxStackElementPadding);

    AllocaInst *AI = *It;
    IRB.SetInsertPoint(*It);  //Go to the point where a new alloca inst is inserted
    Type *Ty = AI->getAllocatedType();
    unsigned Align = AI->getAlignment();
    paddingSize = paddingSize * Align;  //Padding size should abide by alignment
    Value *arraySize = ConstantInt::get(Type::getInt8Ty(getGlobalContext()),paddingSize,false);   
    IRB.CreateAlloca(Type::getInt8Ty(getGlobalContext()),arraySize,"");  //Allocation for padding
    DEBUG(dbgs() << "Adding a pad with size: " << paddingSize << " at function " << Fn.getName() << " before :");
    DEBUG(AI->dump());
    DEBUG(dbgs() << "\n");
  }
   
  return true;
}

}//End of namespace



char StackElementPadding::ID = 0;

INITIALIZE_PASS(StackElementPadding, "stack-element-padding",
                                    "Pass for insert padding between elements", false, false) 

Pass *llvm::createStackElementPaddingPass(const TargetMachine *TM)
{
    return new StackElementPadding(TM);
}
