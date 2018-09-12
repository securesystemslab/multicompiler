//===- HeapChecks.cpp - Heap Cross-Checks ---------------------------------===//

#include <string>
#include <fstream>

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/DataRando/Passes.h"

#define DEBUG_TYPE "HeapChecks"

using namespace llvm;

STATISTIC(NumHeapCrossChecks, "Number of variant heap cross-checks");
STATISTIC(NumHeapFlushes, "Number of variant heap crosscheck points");

static cl::opt<bool> HeapCheckHash("hash-heap-checks",
				   cl::desc("Batch heap-checks using a hash"));
static cl::opt<bool> HeapCheckDebug("debug-heap-checks",
                                    cl::desc("Enable heap crosscheck debugging"));

class HeapChecks : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  HeapChecks() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

char HeapChecks::ID = 0;
static RegisterPass<HeapChecks> X("heapchecks", "Heap cross-checking pass");

struct HeapCheckVisitor : public InstVisitor<HeapCheckVisitor> {
  explicit HeapCheckVisitor(DenseSet<Function *> *blacklist)
    : toCheck(), toFlush(), BlackList(blacklist) {};

  void checkPointer(Instruction &I, Value *ptr) {
    auto GV = dyn_cast<GlobalVariable>(ptr);
    if (GV && GV->isNoCrossCheck())
      return;
    toCheck.insert(std::make_pair(&I, ptr));
  }

  void visitLoadInst(LoadInst &L) {
    checkPointer(L, L.getPointerOperand());
  }

  void visitStoreInst(StoreInst &S) {
    checkPointer(S, S.getPointerOperand());
  }

  void visitCallSite(CallSite CS) {
    Function *fun = CS.getCalledFunction();
    if (fun != NULL
	&& fun->isDeclaration()
	&& !fun->isIntrinsic()
	&& (BlackList->find(fun) == BlackList->end()))
      toFlush.insert(CS.getInstruction());
  }

  DenseSet<std::pair<Instruction *, Value *>> toCheck;
  DenseSet<Instruction *> toFlush;
private:
  DenseSet<Function *> *BlackList;
};

bool HeapChecks::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();
  FunctionType *CheckFnTy, *FlushFnTy, *EnterFnTy;
  Constant *CheckFn, *FlushFn, *EnterFn;
  if (HeapCheckDebug) {
    CheckFnTy = FunctionType::get(Type::getVoidTy(C),
				  {Type::getInt8PtrTy(C),
				   Type::getInt8PtrTy(C),
				   Type::getInt32Ty(C),
				   Type::getInt32Ty(C),
				   Type::getInt8PtrTy(C)},
				  false);
    if (HeapCheckHash)
      CheckFn = M.getOrInsertFunction("__crosscheckHashObjectDebug", CheckFnTy);
    else
      CheckFn = M.getOrInsertFunction("__crosscheckObjectDebug", CheckFnTy);
    FlushFnTy = FunctionType::get(Type::getVoidTy(C),
                                  {Type::getInt8PtrTy(C), Type::getInt8PtrTy(C)},
                                  false);
    FlushFn = M.getOrInsertFunction("__crosscheckHashDebug", FlushFnTy);
    EnterFnTy = FunctionType::get(Type::getVoidTy(C),
                                  {Type::getInt8PtrTy(C)},
                                  false);
    EnterFn = M.getOrInsertFunction("__crosscheckEnterDebug", EnterFnTy);
  } else {
    CheckFnTy = FunctionType::get(Type::getVoidTy(C),
                                  {Type::getInt8PtrTy(C)},
                                  false);
    if (HeapCheckHash)
      CheckFn = M.getOrInsertFunction("__crosscheckHashObject", CheckFnTy);
    else
      CheckFn = M.getOrInsertFunction("__crosscheckObject", CheckFnTy);
    FlushFnTy = FunctionType::get(Type::getVoidTy(C), {}, false);
    FlushFn = M.getOrInsertFunction("__crosscheckHash", FlushFnTy);
  }

  bool modified = false;

  DenseSet<Function *> blackList;
  Function *fun;

  fun = M.getFunction("__crosscheck");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckObject");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckObjectDebug");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckHashObject");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckHashObjectDebug");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckHash");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckHashDebug");
  if (fun)
    blackList.insert(fun);

  fun = M.getFunction("__crosscheckEnterDebug");
  if (fun)
    blackList.insert(fun);

  for (auto &F : M) {
    if (!F.hasFnAttribute(Attribute::CrossCheck))
      continue;
    if (F.getName().startswith("drrt_"))
      continue;
    if (F.getName().startswith("__crosscheck"))
      continue;

    HeapCheckVisitor HCV(&blackList);
    HCV.visit(F);

    for (auto &InstValuePair : HCV.toCheck) {
      Instruction *I = InstValuePair.first;
      Value *ptr = InstValuePair.second;
      IRBuilder<> builder(I);
      builder.SetInsertPoint(I);
      if (HeapCheckDebug) {
        Value *caller = builder.CreateGlobalStringPtr(F.getName());
	Value *line, *col, *file;
	if (MDNode *N = I->getMetadata("dbg")) {
	  line = ConstantInt::get(Type::getInt32Ty(I->getContext()),
				  dyn_cast<DILocation>(N)->getLine());
	  col = ConstantInt::get(Type::getInt32Ty(I->getContext()),
				 dyn_cast<DILocation>(N)->getColumn());
	  file = builder.CreateGlobalStringPtr(dyn_cast<DILocation>(N)->getFilename());
	} else {
	  line = ConstantInt::get(Type::getInt32Ty(I->getContext()), -1);
	  col = ConstantInt::get(Type::getInt32Ty(I->getContext()), -1);
	  file = builder.CreateGlobalStringPtr("unknown");
	}
        Value *ptrToByte =
	  builder.CreateCast(Instruction::CastOps::BitCast, ptr, Type::getInt8PtrTy(M.getContext()));
        builder.CreateCall(CheckFnTy, CheckFn, { caller, file, line, col, ptrToByte });
      } else {
        Value *ptrToByte =
	  builder.CreateCast(Instruction::CastOps::BitCast, ptr, Type::getInt8PtrTy(M.getContext()));
        builder.CreateCall(CheckFnTy, CheckFn, {ptrToByte});
      }
      NumHeapCrossChecks++;
    }

    if (HeapCheckHash) {
      for (auto &Inst : HCV.toFlush) {
	IRBuilder<> builder(Inst);
	builder.SetInsertPoint(Inst);
	if (HeapCheckDebug) {
	  Value *caller = builder.CreateGlobalStringPtr(F.getName());
	  Value *callee =
	    builder.CreateGlobalStringPtr(dyn_cast<CallInst>(Inst)->getCalledFunction()->getName());
	  builder.CreateCall(FlushFnTy, FlushFn, { caller, callee });
	} else {
	  builder.CreateCall(FlushFnTy, FlushFn, {});
	}
	NumHeapFlushes++;
      }
    }

    if (HeapCheckDebug & !F.empty()) {
      BasicBlock *entry = &F.getEntryBlock();
      IRBuilder<> builder(entry);
      if (!entry->empty())
        builder.SetInsertPoint(entry, entry->begin());
      Value *fnName = builder.CreateGlobalStringPtr(F.getName());
      builder.CreateCall(EnterFnTy, EnterFn, { fnName });
      modified = true;
    }

    modified |= (!HCV.toCheck.empty());
    modified |= (HeapCheckHash & (!HCV.toFlush.empty()));
  }

  return modified;
}

void HeapChecks::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

namespace llvm {
  ModulePass *createHeapChecksPass() { return new HeapChecks(); }
}
