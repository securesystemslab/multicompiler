//===-- PointerProtection.cpp: Pointer Protection -------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License.
// Copyright (c) 2015 The Regents of the University of California
// Copyright (c) 2015-2016 Immunant, Inc.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief 
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Passes.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <vector>

using namespace llvm;

#define DEBUG_TYPE "pp"

static cl::opt<int>
DisjointTrampolineSpacing(
  "disjoint-trampoline-spacing",
  cl::desc("Arrange trampolines with given spacing distance"),
  cl::init(0));

static cl::opt<int>
DisjointTrampolineMultiple(
  "disjoint-trampoline-multiple",
  cl::desc("Do not emit a trampoline at multiples of the given offset"),
  cl::init(0));

STATISTIC(NumJumpTrampolines, "Number of Jump Trampolines emitted");

typedef IRBuilder<true, TargetFolder> BuilderTy;

namespace {
class InsertCallback {
public:
  virtual void Insert(BuilderTy *Builder, SmallVectorImpl<int> &Idxs) = 0;
protected:
  virtual ~InsertCallback() = 0;
};

class InsertRemask : public InsertCallback {
public:
  void Insert(BuilderTy *Builer, SmallVectorImpl<int> &Idxs) override;

  InsertRemask(Value *Src, Value *Dest, BasicBlock *FailBlock) :
    Src(Src), Dest(Dest), FailBlock(FailBlock) {}

  BasicBlock *getFailBlock() { return FailBlock; }

private:
  Value *Src;
  Value *Dest;
  BasicBlock *FailBlock;
};

class InsertGlobalHMAC : public InsertCallback {
public:
  void Insert(BuilderTy *Builer, SmallVectorImpl<int> &Idxs) override;

  InsertGlobalHMAC(GlobalVariable &G) : G(&G) {}

private:
  GlobalVariable *G;
};

class PointerProtection : public ModulePass {
public:
  static char ID;

  PointerProtection()
      : ModulePass(ID), HMACForwardPointers(false) {
    initializePointerProtectionPass(*PassRegistry::getPassRegistry());
  }

  PointerProtection(bool HMACForwardPointers)
      : ModulePass(ID), HMACForwardPointers(HMACForwardPointers) {
    initializePointerProtectionPass(*PassRegistry::getPassRegistry());
  }

  virtual ~PointerProtection();
  bool runOnModule(Module &M) override;
  const char *getPassName() const override { return "Function Address Protection"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool HMACForwardPointers;

  bool ProtectFnUses(Function &F);

  bool ProtectGlobal(GlobalVariable &G);
  bool WalkGlobalInitializer(Constant *Init);

  bool TranslateFnPtrLoads(Function &F);

  bool visitStore(StoreInst &SI);
  bool visitLoad(LoadInst &LI);
  bool visitExtractValue(ExtractValueInst &EV);
  bool visitMemTransfer(MemTransferInst &MI);
  bool visitManualLoad(CallSite CS);

  bool WalkType(Type *StartingType, InsertCallback &Callback);
  void InitializeHashTable();
  Function *getGlobalCtor();

  std::vector<Use*> FnAddressTakenUses(Function &F);
  unsigned getJumpTrampolineIndex(Function *F);
  void CreateTrampoline(Function *F);
  GlobalVariable *GetTrampolineTable();
  Constant *GetTrampolineAddress(Constant *IndexValue);
  void InitializeTrampolineTable();
  void RandomizeTrampolineTable();
  void AddDisjointPadding();

  Module *CurModule;
  BuilderTy *Builder;

  Function *GlobalCtor = nullptr;

  // Currently only used inside TranslateFnPtrLoads. This will need to be a
  // Function->BB map if it's needed elsewhere
  BasicBlock *CurFailBlock = nullptr;

  // Map from function to index in JumpTrampolineTable
  DenseMap<Function*, unsigned> JumpTrampolineMap;
  // Table of jump trampolines in emission order. Second element of item is
  // function* or NULL to update map when this ordering changes
  std::vector<std::pair<Trampoline*, Function*> > JumpTrampolineTable;
};

class CookieProtection : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit CookieProtection() : ModulePass(ID) {
    initializeCookieProtectionPass(*PassRegistry::getPassRegistry());
  }
  bool runOnModule(Module &M) override;

  const char *getPassName() const override { return "Cookie Inserter"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
  }

private:
  uint64_t GlobalCookie;
  ValueMap<const Function*, uint64_t> FunctionCookies;
  uint64_t getFunctionCookie(Function *F);

  BuilderTy *Builder;

  std::unique_ptr<RandomNumberGenerator> RNG;

  ValueMap<const Function*, Function *> CheckFunctions;
  void replaceDirectCallers(Value *Old, Function *New);
  Function* GetCheckFunction(Function *F);

  void InsertSetCookie(Function *F, Instruction *I);

  void AddPrologueCheck(Function &F);
  void AddEpilogueSet(Function &F);
  void InstrumentCalls(Function &F);

  unsigned ClearRegister(BasicBlock &BB, BasicBlock::iterator I,
                         unsigned Reg);

};
} // end anonymous namespace

static BasicBlock *CreateFailBlock(Function &F) {
  BasicBlock *FailBlock = BasicBlock::Create(F.getContext(), "check_fail", &F);

  DebugLoc DL;
  IRBuilder<> Builder(FailBlock);
  Builder.SetCurrentDebugLocation(DL);
  Function *TrapF = Intrinsic::getDeclaration(F.getParent(), Intrinsic::trap);
  CallInst *TrapCall = Builder.CreateCall(TrapF);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  Builder.CreateUnreachable();

  return FailBlock;
}

static CallInst *CreateHMAC(Value *Index, Value *Addr, BuilderTy *Builder) {
  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  Type *PtrTy = Type::getInt8PtrTy(M->getContext());
  Type *IndexTy = Type::getInt64Ty(M->getContext());
  // Function *HMACFn = Intrinsic::getDeclaration(M, Intrinsic::hmac_ptr);
  AttrBuilder B;
  B.addAttribute(Attribute::ReadOnly).addAttribute(Attribute::ReadNone);
  AttributeSet ReadOnlyNoneAttrs =
    AttributeSet::get(M->getContext(), AttributeSet::FunctionIndex, B);
  auto HMACFn = M->getOrInsertFunction("__llvm_hmac_ptr", ReadOnlyNoneAttrs,
                                       PtrTy, IndexTy, PointerType::getUnqual(PtrTy), NULL);
  Addr = Builder->CreatePointerCast(Addr, PointerType::getUnqual(PtrTy));
  return Builder->CreateCall(HMACFn, {Index, Addr});
}

static void InsertCheckPtr(Value *FnPtr, Value *Addr, BasicBlock *CheckBlock,
                           BasicBlock *FailBlock, BasicBlock *ContinueBlock,
                           BasicBlock *PassBlock = nullptr) {
  if (!PassBlock)
    PassBlock = ContinueBlock;
  LLVMContext &C = CheckBlock->getContext();
  Function *F = CheckBlock->getParent();
  Module *M = F->getParent();

  BasicBlock *CheckHMACBlock = BasicBlock::Create(C, "check_hmac", F, PassBlock);

  IRBuilder<false> Builder(CheckBlock->getTerminator());

  Type *UIntPtrTy = Type::getInt64Ty(C);
  Value *FnPtrInt = Builder.CreatePtrToInt(FnPtr, UIntPtrTy);
  Value *IsNull = Builder.CreateICmpEQ(FnPtrInt, ConstantInt::get(UIntPtrTy, 0));
  CheckBlock->getTerminator()->eraseFromParent();
  BranchInst::Create(ContinueBlock, CheckHMACBlock, IsNull, CheckBlock);

  Builder.SetInsertPoint(CheckHMACBlock);
  Type *PtrTy = Type::getInt8PtrTy(C);
  // Function *CheckFn = Intrinsic::getDeclaration(M, Intrinsic::check_ptr);
  AttrBuilder B;
  B.addAttribute(Attribute::ReadOnly).addAttribute(Attribute::ReadNone);
  AttributeSet ReadOnlyNoneAttrs =
    AttributeSet::get(M->getContext(), AttributeSet::FunctionIndex, B);
  auto CheckFn = M->getOrInsertFunction("__llvm_check_ptr", ReadOnlyNoneAttrs,
                                        Type::getInt1Ty(C), PtrTy,
                                        PointerType::getUnqual(PtrTy), NULL);
  FnPtr = Builder.CreatePointerCast(FnPtr, PtrTy);
  Addr = Builder.CreatePointerCast(Addr, PointerType::getUnqual(PtrTy));
  CallInst *ValidHMAC = Builder.CreateCall(CheckFn, {FnPtr, Addr});

  BranchInst::Create(PassBlock, FailBlock, ValidHMAC, CheckHMACBlock);

  InlineFunctionInfo IFI;
  InlineFunction(ValidHMAC, IFI);
}

PointerProtection::~PointerProtection() {}

void PointerProtection::getAnalysisUsage(AnalysisUsage &AU) const {
}

void PointerProtection::CreateTrampoline(Function *F) {
  if (!FnAddressTakenUses(*F).empty()) {
    auto it = JumpTrampolineMap.find(F);
    if (it == JumpTrampolineMap.end()) {
      Trampoline *T = Trampoline::Create(F);
      JumpTrampolineTable.emplace_back(T, F);
      JumpTrampolineMap.insert(std::make_pair(F, JumpTrampolineTable.size()-1));
      ++NumJumpTrampolines;
    }
  }
}

std::vector<Use*> PointerProtection::FnAddressTakenUses(Function &F) {
  std::vector<Use *> Uses;
  for (Use &U : F.uses()) {
    User *FU = U.getUser();
    if (isa<BlockAddress>(FU)) {
      // This is handled in AsmPrinter::EmitBasicBlockStart
      continue;
    }
    if (isa<GlobalAlias>(FU)) {
      // These do not need to be indirected
      continue;
    }
    if (isa<Constant>(FU)) {
      // Don't replace calls to bitcasts of function symbols, since they get
      // translated to direct calls. Also, do not replace bitcasts used as
      // personality functions in landing pad instructions.
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(FU)) {
        if (CE->getOpcode() == Instruction::BitCast) {
          // This bitcast must have exactly one user.
          if (CE->user_begin() != CE->user_end()) {
            User *ParentUs = *CE->user_begin();
            if (CallInst *CI = dyn_cast<CallInst>(ParentUs)) {
              CallSite CS(CI);
              Use &CEU = *CE->use_begin();
              if (CS.isCallee(&CEU)) {
                continue;
              }
            }
            if (isa<GlobalAlias>(ParentUs))
              continue;
            if (isa<Trampoline>(ParentUs))
              continue;
            if (Function *ParentFn = dyn_cast<Function>(ParentUs)) {
              if (ParentFn->getPersonalityFn() == CE)
                continue;
            }
          }
        }
      }
    }

    // Personality now is not an operand of LandingPad instruction but it is
    // an attribute of the function.
    if (Function *ParentFn = dyn_cast<Function>(FU)) {
      if (ParentFn->getPersonalityFn() == &F)
        continue;
    }
    if (isa<CallInst>(FU) || isa<InvokeInst>(FU)) {
      ImmutableCallSite CS(cast<Instruction>(FU));
      if (CS.isCallee(&U))
        continue;
    }

    Uses.push_back(&U);
  }

  return Uses;
}

bool PointerProtection::ProtectFnUses(Function &F) {
  Module *M = F.getParent();

  std::vector<Use*> Uses = FnAddressTakenUses(F);
  for (auto U : Uses) {
    User *FU = U->getUser();
    Function *F = dyn_cast<Function>(U->get());

    // We may have already replaced this Use
    if (!F) continue;

    if (isa<Trampoline>(FU))
      continue;

    DEBUG(dbgs() << "Replacing function user: ");
    DEBUG(FU->dump());

    unsigned Index = getJumpTrampolineIndex(F);

    ConstantInt *IndexValue = ConstantInt::get(Type::getInt64Ty(M->getContext()), Index);
    Constant *TrampPtr = GetTrampolineAddress(IndexValue);

    if (Constant *C = dyn_cast<Constant>(FU)) {
      Constant *NewBitCast =
        ConstantExpr::getBitCast(TrampPtr, F->getType());
      if (GlobalValue *GV = dyn_cast<GlobalValue>(FU))
        GV->replaceUsesOfWith(F, NewBitCast);
      else
        C->handleOperandChange(F, NewBitCast, U);
    } else {
      U->set(ConstantExpr::getPointerCast(TrampPtr, F->getType()));
    }
  }

  return !Uses.empty();
}

bool PointerProtection::ProtectGlobal(GlobalVariable &G) {
  if (!G.hasInitializer() ||
      isa<ConstantAggregateZero>(G.getInitializer()))
    return false;

  if (G.getName().startswith("llvm."))
    return false;

  DEBUG(dbgs() << "Walking type for global " << G.getName() << "\n");

  bool modified = false;

  if (Function *F = dyn_cast<Function>(G.getInitializer())) {
    int Index = getJumpTrampolineIndex(F);

    ConstantInt *IndexValue = ConstantInt::get(Type::getInt64Ty(G.getContext()), Index);
    if (HMACForwardPointers) {
      Constant *CastTrampIdx =
        ConstantExpr::getIntToPtr(IndexValue, F->getType());

      G.setInitializer(CastTrampIdx);
    } else {
      Constant *TrampPtr = GetTrampolineAddress(IndexValue);
      G.setInitializer(ConstantExpr::getPointerCast(TrampPtr, F->getType()));
    }

    modified = true;
  } else {
    modified |= WalkGlobalInitializer(G.getInitializer());
  }

  Function *F = getGlobalCtor();
  Builder->SetInsertPoint(&F->getEntryBlock(), F->getEntryBlock().getFirstInsertionPt());

  InsertGlobalHMAC Inserter(G);
  modified |= WalkType(G.getType()->getPointerElementType(), Inserter);

  return modified;
}

bool PointerProtection::WalkGlobalInitializer(Constant *Init) {
  bool Modified = false;

  SmallVector<Use *, 8> UseStack;
  for (Use &U : Init->stripPointerCasts()->operands())
    UseStack.push_back(&U);

  while (!UseStack.empty()) {
    Use *U = UseStack.back();
    UseStack.pop_back();
    Constant *C = cast<Constant>(U->get());
    assert(C != Init && "Recursive global initializer?");

    // DEBUG(T->dump());
    Constant *CU = cast<Constant>(U->getUser());

    if (Function *F = dyn_cast<Function>(C)) {
      int Index = getJumpTrampolineIndex(F);

      ConstantInt *IndexValue = ConstantInt::get(Type::getInt64Ty(Init->getContext()), Index);
      if (HMACForwardPointers) {
        Constant *CastTrampIdx =
          ConstantExpr::getIntToPtr(IndexValue, F->getType());

        CU->handleOperandChange(F, CastTrampIdx, U);
      } else {
        Constant *TrampPtr = GetTrampolineAddress(IndexValue);
        CU->handleOperandChange(F, ConstantExpr::getPointerCast(TrampPtr, F->getType()), U);
      }
        
      Modified = true;
    } else if (!isa<Trampoline>(C) && !isa<GlobalObject>(C)) {
      for (Use &U : C->operands())
        UseStack.push_back(&U);
    }
  }

  return Modified;
}

bool PointerProtection::TranslateFnPtrLoads(Function &F) {
  bool Modified = false;

  SmallVector<StoreInst*, 16> Stores;
  SmallVector<LoadInst*, 16> Loads;
  SmallVector<ExtractValueInst*, 16> ExtractValueInsts;
  SmallVector<MemTransferInst*, 16> MemTransfers;
  SmallVector<CallSite, 16> ManualPtrLoads;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto SI = dyn_cast<StoreInst>(&I)) {
        // We need to look at each store as well, since a function pointer might
        // get loaded then stored to a new place. We don't want to do this if
        // the stored value is a constant.
        Stores.push_back(SI);
      } else if (auto LI = dyn_cast<LoadInst>(&I)) {
        Loads.push_back(LI);
      // } else if (auto EV = dyn_cast<ExtractValueInst>(&I)) {
        // handle extractions of a fn ptr from a struct?
        // TODO re-enable and check this later
        // errs() << "Hey you probably need to handle ExtractValue\n";

        // Need to handle this for method pointers
        // ExtractValueInsts.push_back(EV);
      } else if (auto MI = dyn_cast<MemTransferInst>(&I)) {
        MemTransfers.push_back(MI);
      } else {
        CallSite CS(&I);
        if (CS) {
          if (Function *F = CS.getCalledFunction()) {
            if (F->getIntrinsicID() == Intrinsic::load_ptr_unsafe) {
              // dbgs() << "Found manual pointer load\n";
              ManualPtrLoads.push_back(CS);
            }
          }
        }
      }
    }
  }

  for (auto SI : Stores)
    Modified |= visitStore(*SI);

  for (auto LI : Loads)
    Modified |= visitLoad(*LI);

  // for (auto EV : ExtractValueInsts)
  //   Modified |= visitExtractValue(*EV);
    
  for (auto MI : MemTransfers)
    Modified |= visitMemTransfer(*MI);
    
  for (auto CS : ManualPtrLoads)
    Modified |= visitManualLoad(CS);

  CurFailBlock = nullptr;

  return Modified;
}

bool PointerProtection::visitStore(StoreInst &SI) {
  Value *V = SI.getValueOperand()->stripPointerCasts();
  Value *Address = SI.getPointerOperand();
  auto &Context = SI.getContext();

  Type *VTy = V->getType();

  if (auto CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->getOpcode() == Instruction::PtrToInt) {
      V = CE->getOperand(0);
      VTy = V->getType();
    }
  }

  // Constant fn ptr stores should be taken care of already. um... no it's
  // easier to do it here, then clean up any remaining fn uses and verify that
  // they are safe.
  if (isa<Constant>(V)) {
    if (auto StructValue = dyn_cast<ConstantStruct>(V)) {
      if (StructValue->getType()->hasName()
          && StructValue->getType()->getName() == "llvm.memptr") {
        // this is a method pointer
        if (isa<ConstantVTIndex>(StructValue->getOperand(0)))
          return false;

        ConstantExpr *PtrToInt = cast<ConstantExpr>(StructValue->getOperand(0));
        Function *F = cast<Function>(PtrToInt->getOperand(0));

        int Index = getJumpTrampolineIndex(F);
        ConstantInt *IndexValue = ConstantInt::get(Type::getInt64Ty(Context), Index);

        if (HMACForwardPointers) {
          Builder->SetInsertPoint(&SI);

          Value *PtrAddress = Builder->CreateStructGEP(VTy, Address, 0);
          CallInst *HMACCall = CreateHMAC(IndexValue, PtrAddress, Builder);
          auto HMACedPtr = Builder->CreatePtrToInt(HMACCall,
                                                   Type::getInt64Ty(Context));
          Builder->CreateStore(HMACedPtr, PtrAddress);

          Value *AdjAddress = Builder->CreateStructGEP(VTy, Address, 1);
          Builder->CreateStore(StructValue->getOperand(1), AdjAddress);
          SI.eraseFromParent();
          
          InlineFunctionInfo IFI;
          InlineFunction(HMACCall, IFI);
        } else {
          Constant *TrampolinePtr = GetTrampolineAddress(IndexValue);
          Type *DestTy = SI.getValueOperand()->getType();
          Value *CastedPtr;
          if (DestTy->isIntegerTy())
            CastedPtr = Builder->CreatePtrToInt(TrampolinePtr, DestTy);
          else
            CastedPtr = Builder->CreateBitCast(TrampolinePtr, DestTy);
          SI.setOperand(0, CastedPtr);
        }

        return true;
      }
    }

    if (auto F = dyn_cast<Function>(V)) {
      int Index = getJumpTrampolineIndex(F);
      ConstantInt *IndexValue = ConstantInt::get(Type::getInt64Ty(Context), Index);

      CallInst *HMACCall = nullptr;
      Value *PointerValue;
      if (HMACForwardPointers) {
        Builder->SetInsertPoint(&SI);
        HMACCall = CreateHMAC(IndexValue, Address, Builder);
        PointerValue = HMACCall;
      } else {
        PointerValue = GetTrampolineAddress(IndexValue);
      }

      Type *DestTy = SI.getValueOperand()->getType();
      Value *CastedPtr;
      if (DestTy->isIntegerTy())
        CastedPtr = Builder->CreatePtrToInt(PointerValue, DestTy);
      else
        CastedPtr = Builder->CreateBitCast(PointerValue, DestTy);
      SI.setOperand(0, CastedPtr);

      if (HMACCall) {
        InlineFunctionInfo IFI;
        InlineFunction(HMACCall, IFI);
      }
      return true;
    }

    DEBUG(dbgs() << "Store with constant operand: ");
    DEBUG(V->dump());
    return false;
  }

  if (auto ST = dyn_cast<StructType>(VTy)) {
    if (ST->getName() == "llvm.memptr") {
      Constant *ptrdiff_1 = Builder->getInt64(1);

      BasicBlock *StartingBlock = SI.getParent();
      BasicBlock *ContinueBlock = StartingBlock->splitBasicBlock(std::next(BasicBlock::iterator(SI)));
      auto HMACBlock = BasicBlock::Create(Context, "create_hmac",
                                          StartingBlock->getParent(), ContinueBlock);
      auto FnNonVirtual = BasicBlock::Create(Context, "nonvirtual",
                                             StartingBlock->getParent(), HMACBlock);

      Builder->SetCurrentDebugLocation(SI.getDebugLoc());
      StartingBlock->getTerminator()->eraseFromParent();
      Builder->SetInsertPoint(StartingBlock);
      auto Adj = Builder->CreateExtractValue(V, 1);
      auto IsVirtual = Builder->CreateAnd(Adj, ptrdiff_1);
      IsVirtual = Builder->CreateIsNotNull(IsVirtual, "memptr.isvirtual");
      Builder->CreateCondBr(IsVirtual, ContinueBlock, FnNonVirtual);

      Builder->SetInsertPoint(FnNonVirtual);
      Value *FnPtr = Builder->CreateExtractValue(V, 0);
      Value *IsNull = Builder->CreateICmpEQ(FnPtr, Builder->getInt64(0));
      Builder->CreateCondBr(IsNull, ContinueBlock, HMACBlock);

      Builder->SetInsertPoint(HMACBlock);
      GlobalVariable *TT = GetTrampolineTable();
      Value *TTInt = Builder->CreatePtrToInt(TT, Type::getInt64Ty(Context));
      Value *Distance = Builder->CreateSub(FnPtr, TTInt);

      // WARNING WARNING WARNING
      // This really should query the DataLayout to find the proper type
      // size. However, apparently the DataLayout doesn't know how big a
      // trampoline is yet.
      // FIXME later
      // uint64_t TrampSize = DL->getTypeSizeInBits(Type::getTrampolineTy(Context))/8;
      uint64_t TrampSize = 8;

      Value *Index = Builder->CreateExactSDiv(Distance, ConstantInt::get(Type::getInt64Ty(Context), TrampSize));
      // Index = Builder->CreateTrunc(Index, Type::getInt16Ty(Context));

      auto PtrAddress = Builder->CreateStructGEP(VTy, Address, 0);
      CallInst *HMACCall = CreateHMAC(Index, Address, Builder);
      auto HMACedPtr = Builder->CreatePtrToInt(HMACCall,
                                               Type::getInt64Ty(Context));

      Builder->CreateStore(HMACedPtr, PtrAddress);
      Builder->CreateBr(ContinueBlock);

      InlineFunctionInfo IFI;
      InlineFunction(HMACCall, IFI);
      return true;
    }
  }

  if (VTy->isPointerTy() && VTy->getPointerElementType()->isFunctionTy()) {
    // First we need to check whether the incoming pointer is NULL. If so, do
    // not bother HMACing

    auto StartingBlock = SI.getParent();
    auto ContinueBlock = StartingBlock->splitBasicBlock(&SI);
    auto HMACBlock = BasicBlock::Create(Context, "create_hmac", StartingBlock->getParent(), ContinueBlock);
    
    Builder->SetInsertPoint(StartingBlock->getTerminator());
    Type *UIntPtrTy = Type::getInt64Ty(Context);
    Value *FnPtrInt = Builder->CreatePtrToInt(V, UIntPtrTy);
    Value *IsNull = Builder->CreateICmpEQ(FnPtrInt, ConstantInt::get(UIntPtrTy, 0));
    StartingBlock->getTerminator()->eraseFromParent();
    BranchInst::Create(ContinueBlock, HMACBlock, IsNull, StartingBlock);

    Builder->SetInsertPoint(HMACBlock);
    Value *TrampPtr = Builder->CreatePointerCast(V, Type::getTrampolinePtrTy(Context));
    GlobalVariable *TT = GetTrampolineTable();
    Value *TrampPtrInt = Builder->CreatePtrToInt(TrampPtr, Type::getInt64Ty(Context));
    Value *TTInt = Builder->CreatePtrToInt(TT, Type::getInt64Ty(Context));
    Value *Distance = Builder->CreateSub(TrampPtrInt, TTInt);

    // WARNING WARNING WARNING
    // This really should query the DataLayout to find the proper type
    // size. However, apparently the DataLayout doesn't know how big a
    // trampoline is yet.
    // FIXME later
    // uint64_t TrampSize = DL->getTypeSizeInBits(Type::getTrampolineTy(Context))/8;
    uint64_t TrampSize = 8;

    Value *Index = Builder->CreateExactSDiv(Distance, ConstantInt::get(Type::getInt64Ty(Context), TrampSize));
    // Index = Builder->CreateTrunc(Index, Type::getInt16Ty(Context));

    Type *PtrTy = Type::getInt8PtrTy(Context);
    Address = Builder->CreatePointerCast(Address, PointerType::getUnqual(PtrTy));
    CallInst *HMACCall = CreateHMAC(Index, Address, Builder);
    Value *CastedPtr = Builder->CreatePointerCast(HMACCall,
                                                  V->getType());
    Builder->CreateBr(ContinueBlock);

    Builder->SetInsertPoint(&SI);
    PHINode *PHI = Builder->CreatePHI(V->getType(), 2);
    PHI->addIncoming(V, StartingBlock);
    PHI->addIncoming(CastedPtr, HMACBlock);

    SI.replaceUsesOfWith(V, PHI);
    DEBUG(dbgs() << "Replacing " << V->getName() << " with HMACed " << HMACCall->getName() << "\n");

    InlineFunctionInfo IFI;
    InlineFunction(HMACCall, IFI);
    return true;
  }

  if (VTy->isStructTy()) {
    DEBUG(dbgs() << "Looking at struct store: ");
    DEBUG(SI.dump());
  }

  return false;
}

unsigned PointerProtection::getJumpTrampolineIndex(Function *F) {
  auto it = JumpTrampolineMap.find(F);
  if (it == JumpTrampolineMap.end()) {
    Trampoline *T = Trampoline::Create(F);
    unsigned index = JumpTrampolineMap.size();
    JumpTrampolineTable.emplace_back(T, F);
    JumpTrampolineMap.insert(std::make_pair(F, index));
    dbgs() << "Warning: Adding a trampoline after shuffling trampoline table\n";
    return index;
  }
  if (F->hasName())
    DEBUG(dbgs() << "Found index " << it->second << " for function " << F->getName());
  return it->second;
}

GlobalVariable *PointerProtection::GetTrampolineTable() {
  GlobalVariable *TT = CurModule->getNamedGlobal("llvm.trampoline_table");
  if (!TT) {
    TT = new GlobalVariable(*CurModule, 
                            PointerType::get(Type::getTrampolineTy(CurModule->getContext()), 0),
                            true, GlobalValue::InternalLinkage, nullptr,
                            "llvm.trampoline_table");
  }
  return TT;
}

Constant *PointerProtection::GetTrampolineAddress(Constant *IndexValue) {
  GlobalVariable *TT = GetTrampolineTable();
  SmallVector<Constant *, 1> GEPIndices;
  GEPIndices.push_back(IndexValue);
  auto Address = ConstantExpr::getGetElementPtr(TT->getValueType(), TT, GEPIndices);
  return Address;
}

bool PointerProtection::visitLoad(LoadInst &LI) {
  Value *Addr = LI.getPointerOperand();
  Type *ObjectType = Addr->getType()->getPointerElementType();
  auto &Context = LI.getContext();
  
  if (auto ST = dyn_cast<StructType>(ObjectType)) {
    if (ST->hasName() && ST->getName() == "llvm.memptr") {
      for (auto U : LI.users()) {
        if (auto EV = dyn_cast<ExtractValueInst>(U)) {
          for (auto U : EV->users()) {
            if (auto IP = dyn_cast<IntToPtrInst>(U)) {
              if (!CurFailBlock)
                CurFailBlock = CreateFailBlock(*EV->getParent()->getParent());

              Builder->SetInsertPoint(IP);
              Type *PtrTy = Type::getInt8PtrTy(Context);
              auto FnPtr = Builder->CreateIntToPtr(EV, PtrTy);
              Addr = Builder->CreatePointerCast(Addr, PointerType::getUnqual(PtrTy));

              BasicBlock *CheckBlock = IP->getParent();
              auto SplitPoint = std::next(BasicBlock::iterator(IP));
              BasicBlock *ContinueBlock = CheckBlock->splitBasicBlock(SplitPoint);
              BasicBlock *PassBlock = BasicBlock::Create(Context, "hmac_pass", ContinueBlock->getParent(), ContinueBlock);
              InsertCheckPtr(FnPtr, Addr, CheckBlock, CurFailBlock, ContinueBlock, PassBlock);
              Builder->SetCurrentDebugLocation(DebugLoc());
              Builder->SetInsertPoint(PassBlock);

              GlobalVariable *TT = GetTrampolineTable();

              Value *Index = Builder->CreatePtrToInt(FnPtr, Type::getInt64Ty(Context));
              SmallVector<Value *, 1> GEPIndices;
              // GEPIndices.push_back(ConstantInt::get(Type::getInt16Ty(Context), 0));
              GEPIndices.push_back(Index);
              Value *TrampPtr = Builder->CreateGEP(TT, GEPIndices);
              Value *CastedPtr = Builder->CreatePointerCast(TrampPtr,
                                                            IP->getType());
              Builder->CreateBr(ContinueBlock);

              Builder->SetInsertPoint(&*ContinueBlock->getFirstInsertionPt());
              PHINode *PHI = Builder->CreatePHI(IP->getType(), 2);
              PHI->addIncoming(ConstantPointerNull::get(cast<PointerType>(IP->getType())), CheckBlock);
              PHI->addIncoming(CastedPtr, PassBlock);

              IP->replaceAllUsesWith(PHI);
              IP->eraseFromParent();
              return true;
            }
          }
        }
      }
    }
  }


  if (ObjectType->isPointerTy() &&
      ObjectType->getPointerElementType()->isFunctionTy()) {
    if (auto GEP = dyn_cast<GetElementPtrInst>(Addr)) {
      for (auto OI = GEP->idx_begin(), OE = GEP->idx_end(); OI != OE; ++OI) {
        if (isa<ConstantVTIndex>(OI))
          return false;
      }
    }

    if (Addr->getName().startswith("vtable")) {
      // FIXME This is a TOTAL hack. VTable derefs should be marked by vtable
      // rando. However, to get this working without vtable rando and
      // boobytrapped vtables, I need a hack. Once I build a vtable rando
      // version of libc++, I can take this out
      return false;
    }
    DEBUG(dbgs() << "Checking fn ptr load: ");
    DEBUG(LI.dump());

    SmallVector<Use*, 16> Uses;
    for (auto &U : LI.uses())
      Uses.push_back(&U);

    if (!CurFailBlock)
      CurFailBlock = CreateFailBlock(*LI.getParent()->getParent());

    BasicBlock *CheckBlock = LI.getParent();
    auto SplitPoint = std::next(BasicBlock::iterator(&LI));
    BasicBlock *ContinueBlock = CheckBlock->splitBasicBlock(SplitPoint);
    BasicBlock *PassBlock = BasicBlock::Create(Context, "hmac_pass", ContinueBlock->getParent(), ContinueBlock);
    InsertCheckPtr(&LI, Addr, CheckBlock, CurFailBlock, ContinueBlock, PassBlock);
    Builder->SetCurrentDebugLocation(DebugLoc());
    Builder->SetInsertPoint(PassBlock);

    GlobalVariable *TT = GetTrampolineTable();

    Value *Index = Builder->CreatePtrToInt(&LI, Type::getInt64Ty(Context));
    SmallVector<Value *, 1> GEPIndices;
    // GEPIndices.push_back(ConstantInt::get(Type::getInt16Ty(Context), 0));
    GEPIndices.push_back(Index);
    Value *TrampPtr = Builder->CreateGEP(TT, GEPIndices);
    Value *CastedPtr = Builder->CreatePointerCast(TrampPtr,
                                                  LI.getType());
    Builder->CreateBr(ContinueBlock);

    Builder->SetInsertPoint(&*ContinueBlock->getFirstInsertionPt());
    PHINode *PHI = Builder->CreatePHI(LI.getType(), 2);
    PHI->addIncoming(ConstantPointerNull::get(cast<PointerType>(LI.getType())), CheckBlock);
    PHI->addIncoming(CastedPtr, PassBlock);

    for (Use *U : Uses) {
      User *UI = U->getUser();
      if (auto *C = dyn_cast<Constant>(UI)) {
        if (!isa<GlobalValue>(C)) {
          C->handleOperandChange(&LI, PHI, U);
          continue;
        }
      }
      U->set(PHI);
    }
  }

  return false;
}

bool PointerProtection::visitMemTransfer(MemTransferInst &MI) {
  Value *Src = MI.getSource();
  Value *Dest = MI.getDest();
  Type *SrcTy = Src->getType()->getPointerElementType();

  Builder->SetInsertPoint(&*std::next(BasicBlock::iterator(&MI)));

  if (Src->getType() != Dest->getType())
    Dest = Builder->CreateBitCast(Dest, Src->getType());

  InsertRemask Inserter(Src, Dest, CurFailBlock);

  bool modified = WalkType(SrcTy, Inserter);

  if (!CurFailBlock && modified)
    CurFailBlock = Inserter.getFailBlock();

  return modified;
}

bool PointerProtection::visitManualLoad(CallSite CS) {
  Value *FnPtr = CS.getArgument(0);
  GlobalVariable *TT = GetTrampolineTable();

  Builder->SetInsertPoint(CS.getInstruction());
  Value *Index = Builder->CreatePtrToInt(FnPtr, Type::getInt64Ty(CS->getContext()));
  SmallVector<Value *, 1> GEPIndices;
  GEPIndices.push_back(Index);
  Value *TrampPtr = Builder->CreateGEP(TT, GEPIndices);
  Value *CastedPtr = Builder->CreatePointerCast(TrampPtr,
                                                CS->getType());

  CS->replaceAllUsesWith(CastedPtr);
  CS->eraseFromParent();
  return true;
}

void InsertRemask::Insert(BuilderTy *Builder, SmallVectorImpl<int> &Idxs) {
  SmallVector<Value *, 8> IdxValues;
  for (auto I : Idxs) {
    IdxValues.push_back(ConstantInt::get(Type::getInt32Ty(Src->getContext()), I));
  }

  // After a memcpy of a function pointer we need to
  // - Load the source function pointer
  // - check that its HMAC is still valid
  // - if valid:
  //   - compute a new HMAC for the destination address
  //   - store the newly HMACed pointer to the destination
  // if it's not valid, it might be an uninitialized pointer or have been corrupted
  // by an attacker. just ignore this, any future use will fail

  Value *Addr = Builder->CreateGEP(Src, IdxValues);
  LoadInst *FnPtr = Builder->CreateLoad(Addr);

  // Check the loaded index|hmac to make sure it wasn't altered
  if (!FailBlock)
    FailBlock = CreateFailBlock(*FnPtr->getParent()->getParent());

  BasicBlock *CheckBlock = FnPtr->getParent();
  auto SplitPoint = std::next(BasicBlock::iterator(FnPtr));
  BasicBlock *ContinueBlock = FnPtr->getParent()->splitBasicBlock(SplitPoint);
  BasicBlock *PassBlock = BasicBlock::Create(FnPtr->getContext(), "hmac_pass",
                                             ContinueBlock->getParent(), ContinueBlock);

  InsertCheckPtr(FnPtr, Addr, CheckBlock, ContinueBlock, ContinueBlock, PassBlock);

  Builder->SetInsertPoint(PassBlock);

  // now recreate the hmac and store to the new location
  Value *NewAddr = Builder->CreateGEP(Dest, IdxValues);
  Value *Index = Builder->CreatePtrToInt(FnPtr, Type::getInt64Ty(Src->getContext()));
  CallInst *HMACCall = CreateHMAC(Index, NewAddr, Builder);
  Value *CastedPtr = Builder->CreateBitCast(HMACCall,
                                            FnPtr->getType());
  Builder->CreateStore(CastedPtr, NewAddr);
  Instruction *Branch = Builder->CreateBr(ContinueBlock);

  InlineFunctionInfo IFI;
  InlineFunction(HMACCall, IFI);

  Builder->SetInsertPoint(Branch);
}

void InsertGlobalHMAC::Insert(BuilderTy *Builder, SmallVectorImpl<int> &Idxs) {
  DEBUG(dbgs() << "Initializing a global fn ptr: ");
  DEBUG(G->dump());

  // FIXME: Ideally we'd like these constants to be mapped RO and just
  // mprotected before initialization. Actually, we can skip HMAC verification
  // for constant, RO globals. Unfortunately the load site has no way to know if
  // the address is RO in the general case. I don't even think we know at
  // compile-time if the linker will put a value into RO data.
  G->setConstant(false);

  LLVMContext& Context = G->getContext();

  SmallVector<Value *, 8> IdxValues;
  for (auto I : Idxs) {
    IdxValues.push_back(ConstantInt::get(Type::getInt32Ty(Context), I));
  }

  Value *Addr = Builder->CreateGEP(G, IdxValues);
  LoadInst *LI = Builder->CreateLoad(Addr);

  auto StartingBlock = LI->getParent();
  BasicBlock::iterator SplitPoint = std::next(BasicBlock::iterator(LI));
  BasicBlock *ContinueBlock = StartingBlock->splitBasicBlock(SplitPoint);
  auto HMACBlock = BasicBlock::Create(Context, "create_hmac", StartingBlock->getParent(), ContinueBlock);

  Builder->SetInsertPoint(StartingBlock->getTerminator());
  Type *Uint64type = Type::getInt64Ty(Context);
  Value *Index = Builder->CreatePtrToInt(LI, Uint64type);
  Value *IsNull = Builder->CreateICmpEQ(Index, ConstantInt::get(Uint64type, 0));
  StartingBlock->getTerminator()->eraseFromParent();
  BranchInst::Create(ContinueBlock, HMACBlock, IsNull, StartingBlock);

  Type *PtrPtrTy = PointerType::getUnqual(Type::getInt8PtrTy(Context));

  Builder->SetInsertPoint(HMACBlock);
  Value *AddrPtr = Builder->CreatePointerCast(Addr, PtrPtrTy);
  CallInst *HMACCall = CreateHMAC(Index, AddrPtr, Builder);
  Builder->CreateStore(HMACCall, AddrPtr);

  // Can't inline here because we have a degenerate basic block right
  // now. Doesn't really matter anyway, since this is just initialization.
  // InlineFunctionInfo IFI;
  // InlineFunction(HMACCall, IFI);

  Builder->CreateBr(ContinueBlock);
  Builder->SetInsertPoint(&*ContinueBlock->getFirstInsertionPt());
}

bool PointerProtection::WalkType(Type *StartingType, InsertCallback &Callback) {
  bool Modified = false;

  // Iterate over the contained types to find any fn ptrs
  SmallVector<int, 8> Idxs;
  Idxs.push_back(0);

  SmallVector<Type *, 8> TypeStack;
  TypeStack.push_back(StartingType);

  while (!TypeStack.empty()) {
    Type *T = TypeStack.back();
    TypeStack.pop_back();
    if (!T) {
      // we are finished with that struct
      Idxs.pop_back();
      // increment for the struct
      ++Idxs.back();
      continue;
    }

    switch (T->getTypeID()) {
    case Type::PointerTyID: {
      Type *ElementTy = T->getPointerElementType();
      if (ElementTy->isFunctionTy()) {
        Callback.Insert(Builder, Idxs);
        Modified = true;
      }
      break;
    }
    case Type::StructTyID: {
      StructType *ST = cast<StructType>(T);
      TypeStack.push_back(nullptr);
      Idxs.push_back(-1);
      for (int I = ST->getNumElements()-1; I >= 0; --I) {
        TypeStack.push_back(ST->getElementType(I));
      }
      break;
    }
    case Type::ArrayTyID: {
      ArrayType *AT = cast<ArrayType>(T);
      TypeStack.push_back(nullptr);
      Idxs.push_back(-1);
      for (int I = AT->getNumElements()-1; I >= 0; --I) {
        TypeStack.push_back(AT->getElementType());
      }
      break;
    }
    default:
      break;
    }
    ++Idxs.back();
  }

  return Modified;
}

void PointerProtection::InitializeHashTable() {
  Function *F = getGlobalCtor();
  Builder->SetInsertPoint(&*F->getEntryBlock().getFirstInsertionPt());

  auto InitF =
    CurModule->getOrInsertFunction("__llvm_init_masktable",
                                   Type::getVoidTy(F->getContext()),
                                   NULL);

  auto CI = Builder->CreateCall(InitF);
  InlineFunctionInfo IFI;
  bool inlined = InlineFunction(CI, IFI);
  if (inlined)
    if (Function *F = dyn_cast<Function>(InitF))
      F->eraseFromParent();
}

Function *PointerProtection::getGlobalCtor() {
  if (!GlobalCtor) {
    LLVMContext& Context = CurModule->getContext();
    auto FTy = FunctionType::get(Type::getVoidTy(Context), false);
    GlobalCtor = Function::Create(FTy, GlobalValue::InternalLinkage,
                                  "_PointerProtection_global_ctor",
                                  CurModule);
    appendToGlobalCtors(*CurModule, GlobalCtor, 1);

    auto BB = BasicBlock::Create(Context, "entry", GlobalCtor);

    Builder->SetInsertPoint(BB);
    Builder->CreateRetVoid();
  }

  return GlobalCtor;
}

void PointerProtection::InitializeTrampolineTable() {
  if (JumpTrampolineTable.empty())
    return;

  DEBUG(dbgs() << "Before randomization:\n");
  for (auto I : JumpTrampolineTable) {
    if (I.second) {
      DEBUG(dbgs() << "Trampoline table entry for " << I.second->getName() << "\n");
    }
  }

  GlobalVariable *TT = CurModule->getNamedGlobal("llvm.trampoline_table");

  SmallVector<Constant*, 16> Trampolines;
  for (auto I : JumpTrampolineTable) {
    if (I.second) {
      DEBUG(dbgs() << "Adding a trampoline table entry for " << I.second->getName() << "\n");
    }
    Trampolines.push_back(I.first);
  }

  Type *TrampolineTy = Type::getTrampolineTy(CurModule->getContext());
  ArrayType *AT = ArrayType::get(PointerType::get(TrampolineTy, 0),
                                 Trampolines.size());
  Constant *Init = ConstantArray::get(AT, Trampolines);

  auto NewTT = new GlobalVariable(*CurModule, Init->getType(), true,
                                  GlobalValue::InternalLinkage, Init, "llvm.trampoline_table");
  NewTT->setAlignment(8);
  NewTT->setTrampolines(true);
  if (TT) {
    NewTT->takeName(TT);
    Constant *NewTTPtr = ConstantExpr::getPointerCast(NewTT, TT->getType());
    TT->replaceAllUsesWith(NewTTPtr);
    TT->eraseFromParent();
  }
}

void PointerProtection::RandomizeTrampolineTable() {
  std::unique_ptr<RandomNumberGenerator> RNG(CurModule->createRNG());

  std::vector<unsigned> IndexMapping;
  IndexMapping.reserve(JumpTrampolineTable.size());
  for (unsigned i = 0, e = JumpTrampolineTable.size(); i < e; ++i)
    IndexMapping.push_back(i);

  RNG->shuffle(IndexMapping);

  // Copy elements into a new randomly shuffled vector
  std::vector<std::pair<Trampoline*, Function*> > RandomizedTable;
  for (unsigned i = 0, e = JumpTrampolineTable.size(); i < e; ++i) {
    DEBUG(dbgs() << "Moving " << IndexMapping[i] << " to " << i << '\n');
    auto CurTrampEntry = JumpTrampolineTable[IndexMapping[i]];
    RandomizedTable.push_back(CurTrampEntry);
    // Update function map
    if (CurTrampEntry.second)
      JumpTrampolineMap[CurTrampEntry.second] = i;
  }

  JumpTrampolineTable = RandomizedTable;
}

void PointerProtection::AddDisjointPadding() {
  std::vector<std::pair<Trampoline*, Function*> > NewTable;

  // The total number of slots given N levels of tables is
  // round_down(LCM/Spacing)^N
  unsigned TotalSlots = DisjointTrampolineMultiple/DisjointTrampolineSpacing;
  unsigned NumLevels = 1;
  while (TotalSlots < JumpTrampolineTable.size()) {
    TotalSlots *= TotalSlots;
    NumLevels++;
  }

  unsigned CurIndex = 0;

  for (unsigned i = 0, e = JumpTrampolineTable.size(); i < e; ++i) {
    unsigned SlotIndex = (i+1)*DisjointTrampolineSpacing;
    unsigned LevelSize = DisjointTrampolineMultiple;
    for (unsigned level = 0; level < NumLevels; ++level) {
      SlotIndex = SlotIndex % LevelSize + (SlotIndex/LevelSize)*LevelSize*DisjointTrampolineSpacing;
      LevelSize *= DisjointTrampolineMultiple;
    }
    DEBUG(dbgs() << "Slot " << i << " -> " << SlotIndex << '\n');
    
    while (CurIndex < SlotIndex) {
      NewTable.emplace_back(JumpTrampoline::Create(CurModule->getContext()), nullptr);
      ++CurIndex;
    }
    auto CurTrampEntry = JumpTrampolineTable[i];
    NewTable.push_back(CurTrampEntry);
    if (CurTrampEntry.second)
      JumpTrampolineMap[CurTrampEntry.second] = CurIndex;
    ++CurIndex;
  }
  
  JumpTrampolineTable = NewTable;
}

bool PointerProtection::runOnModule(Module &M) {
  CurModule = &M;
  const DataLayout DL = M.getDataLayout();
  BuilderTy TheBuilder(M.getContext(), TargetFolder(DL));
  Builder = &TheBuilder;

  // Insert a NULL trampoline as the first entry. FIXME: Figure out why this is
  // needed. (Also change AddDisjointPadding if changed here)
  // JumpTrampolineTable.insert(std::make_pair(nullptr, JumpTrampoline::Create(M.getContext())));

  DEBUG(dbgs() << "--------------------------------------- BEFORE Pointer Protection ----------------------------------\n");
  DEBUG(M.dump());

  for (auto &F : M)
    CreateTrampoline(&F);

  RandomizeTrampolineTable();

  if (DisjointTrampolineSpacing != 0)
    AddDisjointPadding();

  if (HMACForwardPointers) {
    dbgs() << "HMACing code pointers\n";
    for (auto &F : M)
      TranslateFnPtrLoads(F);

    for (auto &G : M.globals())
      if (!G.isExternallyInitialized())
        ProtectGlobal(G);
  }

  DEBUG(dbgs() << "--------------------------------------- AFTER Handling load/store/globals ----------------------------------\n");
  DEBUG(M.dump());

  for (auto &F : M)
    ProtectFnUses(F);

  // InitializeHashTable();
  InitializeTrampolineTable();

  DEBUG(dbgs() << "--------------------------------------- AFTER Pointer Protection ----------------------------------\n");
  DEBUG(M.dump());

  return true;
}



uint64_t CookieProtection::getFunctionCookie(Function *F) {
  auto I = FunctionCookies.find(F);
  if (I == FunctionCookies.end()) {
    uint64_t FnCookie = RNG->Random() << 32;
    I = FunctionCookies.insert(std::make_pair(F, FnCookie)).first;
    return FnCookie | GlobalCookie;
  }
  return I->second | GlobalCookie;
}

// Replace direct callers of Old with New.
// Shamelessly stolen from lib/Transforms/IPO/MergeFunctions.cpp
void CookieProtection::replaceDirectCallers(Value *Old, Function *New) {
  Constant *BitcastNew = ConstantExpr::getBitCast(New, Old->getType());
  for (auto UI = Old->use_begin(), UE = Old->use_end(); UI != UE;) {
    Use *U = &*UI;
    ++UI;
    if (U->getUser()->stripPointerCasts() == Old) {
      replaceDirectCallers(U->getUser(), New);
    } else {
      CallSite CS(U->getUser());
      if (CS && CS.isCallee(U)) {
        U->set(BitcastNew);
      }
    }
  }
}

// Helper for writeThunk,
// Selects proper bitcast operation,
// but a bit simpler then CastInst::getCastOpcode.
// Shamelessly stolen from lib/Transforms/IPO/MergeFunctions.cpp
static Value *createCast(IRBuilder<false> &Builder, Value *V, Type *DestTy) {
  Type *SrcTy = V->getType();
  if (SrcTy->isStructTy()) {
    assert(DestTy->isStructTy());
    assert(SrcTy->getStructNumElements() == DestTy->getStructNumElements());
    Value *Result = UndefValue::get(DestTy);
    for (unsigned int I = 0, E = SrcTy->getStructNumElements(); I < E; ++I) {
      Value *Element = createCast(
          Builder, Builder.CreateExtractValue(V, makeArrayRef(I)),
          DestTy->getStructElementType(I));

      Result =
          Builder.CreateInsertValue(Result, Element, makeArrayRef(I));
    }
    return Result;
  }
  assert(!DestTy->isStructTy());
  if (SrcTy->isIntegerTy() && DestTy->isPointerTy())
    return Builder.CreateIntToPtr(V, DestTy);
  else if (SrcTy->isPointerTy() && DestTy->isIntegerTy())
    return Builder.CreatePtrToInt(V, DestTy);
  else
    return Builder.CreateBitCast(V, DestTy);
}

// Shamelessly stolen from lib/Transforms/IPO/MergeFunctions.cpp
Function* CookieProtection::GetCheckFunction(Function *F) {
  auto I = CheckFunctions.find(F);
  if (I != CheckFunctions.end())
    return I->second;

  FunctionType *FFTy = F->getFunctionType();

  Function *CheckF = Function::Create(FFTy, F->getLinkage(), "");
  F->getParent()->getFunctionList().insert(F->getIterator(), CheckF);
  BasicBlock *BB = BasicBlock::Create(F->getContext(), "", CheckF);
  IRBuilder<false> Builder(BB);

  CheckF->setName(F->getName() + "_cookiecheck");
  CheckF->setCallingConv(F->getCallingConv());
  CheckF->copyAttributesFrom(F);

  // Linkage must be set after copyAttributesFrom, since private linkage implies
  // default visibility
  // CheckF->setLinkage(GlobalValue::PrivateLinkage);
  // Using internal for testing, can be private later
  CheckF->setLinkage(GlobalValue::InternalLinkage);

  CheckF->addFnAttr(Attribute::NoInline);
  // DON'T MAKE THIS NAKED! Epilogue is not emitted, and the tail call doesn't
  // get selected properly.
  // CheckF->addFnAttr(Attribute::Naked);
  CheckF->addFnAttr(Attribute::CookieCheck);

  replaceDirectCallers(F, CheckF);

  SmallVector<Value *, 16> Args;
  Args.reserve(FFTy->getNumParams());

  unsigned i = 0;
  for (Function::arg_iterator AI = CheckF->arg_begin(), AE = CheckF->arg_end();
       AI != AE; ++AI) {
    Args.push_back(createCast(Builder, &*AI, FFTy->getParamType(i)));
    ++i;
  }

  CallInst *CI = Builder.CreateCall(F, Args);

  // Need to set the call to musttail so that varargs get forwarded
  // correctly. huh.
  CI->setAttributes(F->getAttributes());
  CI->setTailCallKind(CallInst::TCK_MustTail);
  CI->setCallingConv(F->getCallingConv());

  if (CheckF->getReturnType()->isVoidTy()) {
    Builder.CreateRetVoid();
  } else {
    // Casting structure type produces extract/insertvalue instructions,
    // which breaks the rule for musttail call: it must precede a ret with
    // an optional bitcast.
    // Builder.CreateRet(createCast(Builder, CI, CheckF->getReturnType()));
    assert(CI->getType() == CheckF->getReturnType() 
           && "Cookicheck function has a different return type?");
    Builder.CreateRet(CI);
  }

  CheckFunctions[F] = CheckF;

  return CheckF;
}

void CookieProtection::InsertSetCookie(Function *F, Instruction *I) {
  Function *Parent = I->getParent()->getParent();
  uint64_t Cookie = getFunctionCookie(F);
  Value *CookieVal = ConstantInt::get(Type::getInt64Ty(Parent->getContext()),
                                      Cookie);

  Builder->SetInsertPoint(I);
  Function *SetCookieF =
    Intrinsic::getDeclaration(Parent->getParent(),
                              Intrinsic::set_cookie);
  Builder->CreateCall(SetCookieF, CookieVal, "");
}

void CookieProtection::InstrumentCalls(Function &F) {
  auto &Context = F.getContext();
  Module *M = F.getParent();

  // FIXME: Can probably combine both instrumentations into 1 loop, and get rid
  // of the worklists

  // Insert a check after these call sites
  SmallVector<CallSite, 16> CheckList;

  // Insert a set before these
  SmallVector<CallSite, 16> SetList;

  for (auto &BB : F) {
    for (auto &I : BB) {
      CallSite CS(&I);
      if (CS) {
        Value *Callee = CS.getCalledValue();
        if (Function * CalleeF = dyn_cast<Function>(Callee->stripPointerCasts())) {
          if (CalleeF->isIntrinsic()) {
            // FIXME: Is this right? don't we want to do the check before some
            // intrinsics, if they get lowered to a call?
            continue;
          }
          if (CalleeF->isDeclarationForLinker() || 
              // Could be overridden with the function defined in a different binary.
              (M->getPICLevel() != PICLevel::Default && 
               CalleeF->getVisibility() == GlobalValue::DefaultVisibility && 
               CalleeF->getLinkage() != GlobalValue::InternalLinkage)) {
            DEBUG(dbgs() << "Instrumenting call to " << CalleeF->getName() << " with set\n");
            // this is a direct call to an external function
            SetList.push_back(CS);
          } else {
            DEBUG(dbgs() << "Instrumenting call to " << CalleeF->getName() << " with set and check\n");
            SetList.push_back(CS);
            CheckList.push_back(CS);
          }
        }
      }
    }
  }

  BasicBlock *FailBlock;
  if (!CheckList.empty())
    FailBlock = CreateFailBlock(F);

  for (auto CS : SetList) {
    Function *CalledFn = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
    Instruction *CI = CS.getInstruction();
    InsertSetCookie(CalledFn, CI);
  }

  for (auto CS : CheckList) {
    Function *CalledFn = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
    uint64_t Cookie = getFunctionCookie(CalledFn);
    Value *CookieVal = ConstantInt::get(Type::getInt64Ty(Context), Cookie);

    Instruction *CI = CS.getInstruction();
    if (CS.isInvoke())
      // For now, we can't handle invokes correctly. just skip
      continue;

    auto I = std::next(BasicBlock::iterator(CI));
    Builder->SetInsertPoint(&*I);

    auto CheckBlock = CI->getParent();
    BasicBlock *NextBlock = CheckBlock->splitBasicBlock(I);

    Builder->SetInsertPoint(CheckBlock->getTerminator());
    Function *Intrinsic = Intrinsic::getDeclaration(M, Intrinsic::check_cookie);
    Value *Cmp = Builder->CreateCall(Intrinsic, CookieVal, "");

    // Change the unconditional branch to a conditional one
    CheckBlock->getTerminator()->eraseFromParent();
    BranchInst::Create(NextBlock, FailBlock, Cmp, CheckBlock);
  }
}

void CookieProtection::AddPrologueCheck(Function &F) {
  // TODO: fix this hack so that externally referenced functions don't go
  // through the check
  if (F.getName() == "main" || F.hasFnAttribute(Attribute::CookieCheck) ||
      F.isIntrinsic())
    return;

  Function *CheckF = GetCheckFunction(&F);
  Module *M = F.getParent();
  auto &Context = CheckF->getContext();

  auto EntryBlock = CheckF->begin();

  BasicBlock *NewEntryBlock =
    EntryBlock->splitBasicBlock(EntryBlock->getFirstInsertionPt());

  BasicBlock *FailBlock = BasicBlock::Create(Context, "cookie_fail", CheckF);

  Builder->SetInsertPoint(EntryBlock->getTerminator());
  uint64_t Cookie = getFunctionCookie(&F);
  Value *CookieVal = ConstantInt::get(Type::getInt64Ty(Context), Cookie);
  Function *Intrinsic = Intrinsic::getDeclaration(M, Intrinsic::check_cookie);
  Value *Cmp = Builder->CreateCall(Intrinsic, CookieVal, "");

  // Change the unconditional branch to a conditional one
  EntryBlock->getTerminator()->eraseFromParent();
  BranchInst::Create(NewEntryBlock, FailBlock, Cmp, &*EntryBlock);

  Builder->SetInsertPoint(FailBlock);
  Function *TrapF = Intrinsic::getDeclaration(M, Intrinsic::trap);
  CallInst *TrapCall = Builder->CreateCall(TrapF);
  TrapCall->setDoesNotReturn();
  TrapCall->setDoesNotThrow();
  Builder->CreateUnreachable();
}

void CookieProtection::AddEpilogueSet(Function &F) {
  for (auto &BB : F) {
    auto TermI = BB.getTerminator();
    if (isa<ReturnInst>(TermI)) {
      InsertSetCookie(&F, TermI);
    }
  }
}

bool CookieProtection::runOnModule(Module &M) {
  const DataLayout DL = M.getDataLayout();
  BuilderTy TheBuilder(M.getContext(), TargetFolder(DL));
  Builder = &TheBuilder;

  if (!RNG)
    RNG.reset(M.createRNG());
  GlobalCookie = RNG->Random() & 0xffffffff;

  DEBUG(dbgs() << "--------------------------------------- BEFORE Cookie Protection ----------------------------------\n");
  DEBUG(M.dump());

  SmallVector<Function *, 16> Worklist;
  for (auto &F : M) {
    InstrumentCalls(F);
    Worklist.push_back(&F);
  }

  for (auto F : Worklist) {
    AddPrologueCheck(*F);
    AddEpilogueSet(*F);
  }

  DEBUG(dbgs() << "--------------------------------------- AFTER Cookie Protection ----------------------------------\n");
  DEBUG(M.dump());

  return true;
}



InsertCallback::~InsertCallback() {}

char PointerProtection::ID = 0;

INITIALIZE_PASS_BEGIN(PointerProtection, "pointer-protection",
                      "Pointer Protection", true, true)
INITIALIZE_PASS_END(PointerProtection, "pointer-protection",
                    "Pointer Protection", true, true)

ModulePass *llvm::createPointerProtectionPass(bool HMACForwardPointers) {
  return new PointerProtection(HMACForwardPointers);
}


char CookieProtection::ID = 0;
INITIALIZE_PASS(CookieProtection, "cookie-ir-inserter",
                "Cookie Inserter", false, false)

ModulePass *llvm::createCookieProtectionPass() {
  return new CookieProtection();
}
