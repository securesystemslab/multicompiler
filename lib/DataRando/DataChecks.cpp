//===- DataChecks.cpp - Data Cross-Checks ---------------------------------===//

#define DEBUG_TYPE "DataChecks"

#include "llvm/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

namespace {

static cl::opt<bool> EnableDataChecks(
  "xcheck-data", cl::init(false), cl::Hidden,
  cl::desc("Add variant cross-checks for critical data values"));

static cl::opt<bool> EnableRetvalChecks(
  "xcheck-retval", cl::init(false), cl::Hidden,
  cl::desc("Add variant cross-checks for critical return values"));

static cl::opt<bool> CheckAtBranch(
  "data-checks-at-branch", cl::init(false), cl::Hidden,
  cl::desc("Insert conditional cross-checks directly before branches"));

static cl::opt<bool> EnableControlFlowXChecks(
  "xcheck-cf", cl::init(false), cl::Hidden,
  cl::desc("Enable cross-checks on function-level control flow"));

static cl::opt<bool> XCheckLog(
  "log-xchecks", cl::init(false), cl::Hidden,
  cl::desc("Enable data & controlflow crosscheck logging for debugging"));

STATISTIC(NumCrossChecks, "Number of variant data cross-checks");

class DataChecks : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  DataChecks() : ModulePass(ID), DL(nullptr) {}

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  /// Ensure that the cross-check function declaration is available. Lazy
  /// initializer, can be called multiple times.
  void InitializeCheckFn(Module &M);

  void DoConditionChecks(Module &M);

  void DoControlFlowChecks(Module &M);


  void FindConditionsToCheck(Value *Condition, Instruction *U);

  /// Insert checks of conditionals in ConditionsToCheck. Insertion location is
  /// determined by the Location field in each conditional.
  void InsertConditionChecks(LLVMContext &C);

  void CreateCrossCheck(IRBuilder<> &Builder, Value *V);

  const DataLayout *DL;

  // External library function that implements the cross checking. Defined in
  // Runtime/CrossChecks.c and compiled into libDataRando_rt.a
  Constant *CheckFn;
  FunctionType *CheckFnTy;

  enum CheckLocation {
    Branch,
    Load,
  };

  struct ConditionValue {
    Value *V;

    // User, usually a branch, but can be a comparison instruction if not used
    // by a branch.
    Instruction *U;

    CheckLocation Location;
  };

  // Condition values that we want to check, along with the conditional branch
  // that uses that value
  std::vector<ConditionValue> ConditionsToCheck;

  // Comparison instructions that we've already visited. We should avoid
  // cross-checking these when looking for standalone comparison instructions
  // not used in a branch.
  DenseSet<ICmpInst *> VisitedComparisons;
};

}

char DataChecks::ID = 0;
static RegisterPass<DataChecks> X("datachecks", "Data cross-checking pass");

bool DataChecks::runOnModule(Module &M) {
  bool Modified = false;

  if (EnableDataChecks || EnableControlFlowXChecks) {
    InitializeCheckFn(M);
    Modified = true;
  }

  if (EnableDataChecks)
    DoConditionChecks(M);

  if (EnableControlFlowXChecks)
    DoControlFlowChecks(M);

  return Modified;
}

void DataChecks::getAnalysisUsage(AnalysisUsage &AU) const {
}

void DataChecks::InitializeCheckFn(Module &M) {
  if (DL)
    return;

  DL = &M.getDataLayout();
  LLVMContext &C = M.getContext();
  if (XCheckLog) {
    CheckFnTy = FunctionType::get(Type::getVoidTy(C),
                                  {Type::getInt8PtrTy(C),    // function_name
                                      Type::getInt8PtrTy(C), // file
                                      Type::getInt32Ty(C),   // line
                                      Type::getInt32Ty(C),   // col
                                      DL->getIntPtrType(C)}, // value
                                  false);
    CheckFn  = M.getOrInsertFunction("__crosscheckDebug", CheckFnTy);
  } else {
    CheckFnTy = FunctionType::get(Type::getVoidTy(C),
                                  {DL->getIntPtrType(C)},
                                  false);
    CheckFn  = M.getOrInsertFunction("__crosscheck", CheckFnTy);
  }

  // Call the RAVEN cross-check mechanism
}

void DataChecks::DoConditionChecks(Module &M) {
  LLVMContext &C = M.getContext();
  for (auto &F : M) {
    if (!F.hasFnAttribute(Attribute::CrossCheck))
      continue;

    ConditionsToCheck.clear();

    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *Branch = dyn_cast<BranchInst>(&I)) {
          if (Branch->isConditional())
            FindConditionsToCheck(Branch->getCondition(), Branch);
        } else if (auto *Switch = dyn_cast<SwitchInst>(&I)) {
          FindConditionsToCheck(Switch->getCondition(), Switch);
        } else if (auto *Return = dyn_cast<ReturnInst>(&I)) {
          // Now this function is not just for conditions but it
          // also checks sensitive return values.
          if (EnableRetvalChecks && Return->getReturnValue() != nullptr)
            FindConditionsToCheck(Return->getReturnValue(), Return);
        }
      }
    }

    // Take a second pass through looking for any comparisons that aren't used
    // in a branch but that still may need cross-checking.
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *Cmp = dyn_cast<ICmpInst>(&I)) {
          if (VisitedComparisons.count(Cmp) == 0)
            FindConditionsToCheck(Cmp, Cmp);
        }
      }
    }

    if (!ConditionsToCheck.empty()) {
      InitializeCheckFn(M);

      InsertConditionChecks(C);
    }
  }
}

static bool HasTBAAPointerAccess(Value *V) {
  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  MDNode *Tag = I->getMetadata(LLVMContext::MD_tbaa);
  if (!Tag)
    return false;

  if (Tag->getNumOperands() < 2)
    return false;
  MDNode *Type = dyn_cast_or_null<MDNode>(Tag->getOperand(1));
  if (!Type)
    return false;

  if (auto *TypeName = dyn_cast<MDString>(Type->getOperand(0)))
    if (TypeName->getString() == "any pointer")
      return true;

  return false;
}

static bool CanCheckValue(Value *V) {
  if (isa<Constant>(V))
    return false;

  Type *ValTy = V->getType();

  // Pointers are generally not valid to cross check, since they may vary due
  // to randomization. Floating point values might not compare exactly
  // indentical across variants, so ignore for now. Thus, we're left with ints
  if (!ValTy->isIntegerTy())
    return false;

  Value *PointerOperand = nullptr;
  if (auto *I = dyn_cast<LoadInst>(V))
    PointerOperand = I->getPointerOperand();
  else if (auto *I = dyn_cast<VAArgInst>(V))
    PointerOperand = I->getPointerOperand();
  else if (auto *I = dyn_cast<AtomicCmpXchgInst>(V))
    PointerOperand = I->getPointerOperand();

  if (PointerOperand) {
    auto LoadedValue = PointerOperand->stripPointerCasts();

    // Check if we are loading a pointer that has been cast as an int.
    PointerType *OrigType = dyn_cast<PointerType>(LoadedValue->getType());
    if (OrigType && OrigType->getElementType()->isPointerTy())
      return false;

    // Check that we aren't loading a NoCrossCheck global
    auto GV = dyn_cast<GlobalVariable>(LoadedValue);
    if (GV && GV->isNoCrossCheck())
      return false;
  }

  for (auto I = V->user_begin(), E = V->user_end(); I != E; I++ ) {
    auto CI = dyn_cast<CastInst>(*I);
    if (CI && !CI->isIntegerCast())
      return false;
  }

  if (HasTBAAPointerAccess(V))
    return false;

  return true;
}

static bool IsLoad(Value *V) {
  if (isa<LoadInst>(V) ||
      isa<VAArgInst>(V) ||
      isa<AtomicCmpXchgInst>(V))
    return true;

  return false;
}

static bool IsUndefinedPhiRecursive(Value *V) {
  SmallVector<Value*, 4> Worklist;
  DenseSet<const Value *> VisitedValues;
  Worklist.push_back(V);
  VisitedValues.insert(V);

  while (!Worklist.empty()) {
    Value *CurValue = Worklist.back();
    Worklist.pop_back();

    if (!CanCheckValue(CurValue))
      continue;

    if (auto *Phi = dyn_cast<PHINode>(CurValue)) {
      for (Value *InV : Phi->incoming_values()) {
        if (isa<UndefValue>(InV))
          return true;
      }
    }

    if (User *U = dyn_cast<User>(CurValue)) {
      for (Value *V : U->operands())
        if (VisitedValues.insert(V).second)
          Worklist.push_back(V);
    }
  }
  return false;
}

static bool IsValueIntLoadRecursive(Value *V) {
  SmallVector<Value*, 4> Worklist;
  DenseSet<const Value *> VisitedValues;
  Worklist.push_back(V);
  VisitedValues.insert(V);

  while (!Worklist.empty()) {
    Value *CurValue = Worklist.back();
    Worklist.pop_back();

    if (!CanCheckValue(CurValue))
      continue;

    if (IsLoad(CurValue))
      return true;

    if (User *U = dyn_cast<User>(CurValue)) {
      // Do not walk through call instructions. The call arguments are not
      // necessarily directly related to the result, so it makes no sense to
      // cross-check them.
      //
      // Alternatively we could check call return values as if they were loads
      // expected to be datarando'd, but this may add false positives. Will
      // just ignore calls for now.
      if (isa<CallInst>(CurValue) ||
          isa<InvokeInst>(CurValue)) {
        continue;
      }

      for (Value *V : U->operands())
        if (VisitedValues.insert(V).second)
          Worklist.push_back(V);
    }
  }

  return false;
}

void DataChecks::FindConditionsToCheck(Value *Condition, Instruction *U) {
  std::vector<Value*> Worklist;
  DenseSet<const Value *> VisitedValues;
  Worklist.push_back(Condition);
  VisitedValues.insert(Condition);

  while (!Worklist.empty()) {
    Value *CurValue = Worklist.back();
    Worklist.pop_back();

    if (!CanCheckValue(CurValue))
      continue;

    if (auto *Cmp = dyn_cast<ICmpInst>(CurValue))
      VisitedComparisons.insert(Cmp);

    Type *ValTy = CurValue->getType();

    DataChecks::CheckLocation CheckLoc;
    if (CheckAtBranch)
      CheckLoc = DataChecks::Branch;
    else
      CheckLoc = DataChecks::Load;

    if (!ValTy->isIntegerTy(1) &&
        ((isa<TruncInst>(CurValue) &&
         IsValueIntLoadRecursive(CurValue)) ||
         IsLoad(CurValue))) {
      // If this is a truncate of an integer load or a PHI node with an integer
      // load, we should cross-check this value instead of the wider load
      // further up the use chain. Otherwise, we cross-check integer values
      // loaded from memory. We don't walk uses since we are checking this
      // value.
      ConditionsToCheck.push_back({CurValue, U, CheckLoc});
      continue;
    }

    if (isa<PHINode>(CurValue)) {
      // If we are inserting checks at branchs, we can't walk up the use-def
      // chain from PHI nodes because PHI operand defs don't dominate the block
      // where the check will go. Instead, check the PHI value if any of its
      // operands are (potentially recursively) integer loads.
      //
      // We also want to check phis that might depend on undefined values. We
      // need to do this at the branch, so we can't walk past PHIs.
      if ((CheckAtBranch && IsValueIntLoadRecursive(CurValue)) ||
          IsUndefinedPhiRecursive(CurValue)) {
        if (!ValTy->isIntegerTy(1))
          ConditionsToCheck.push_back({CurValue, U, DataChecks::Branch});

        // Even if we shouldn't check this value, don't walk past it since
        // operand defs don't dominate branch.
        continue;
      }
    }

    // If we didn't insert a check for the current value, walk up its use-def
    // chain to check for integer loads.
    if (User *U = dyn_cast<User>(CurValue)) {
      // Do not walk through call instructions. The call arguments are not
      // necessarily directly related to the result, so it makes no sense to
      // cross-check them.
      //
      // Alternatively we could check call return values as if they were loads
      // expected to be datarando'd, but this may add false positives. Will
      // just ignore calls for now.
      if (isa<CallInst>(CurValue) ||
          isa<InvokeInst>(CurValue)) {
        continue;
      }

      for (Value *V : U->operands())
        if (VisitedValues.insert(V).second)
          Worklist.push_back(V);
    }
  }
}

void DataChecks::CreateCrossCheck(IRBuilder<> &Builder, Value *V) {
  IntegerType *PtrIntTy = Builder.getIntPtrTy(*DL);

  // Cast the value to IntPtrType
  auto *VTy = V->getType();
  if (VTy->isPointerTy())
    V = Builder.CreatePtrToInt(V, PtrIntTy);
  else
    V = Builder.CreateZExtOrTrunc(V, PtrIntTy);

  // Call the check function.
  // TODO: Optimize by inlining the call?
  if (XCheckLog) {
    DebugLoc Loc = Builder.GetInsertPoint()->getDebugLoc();
    Value *caller = Builder.CreateGlobalStringPtr(Builder.GetInsertBlock()->getParent()->getName());
    Value *line, *col, *file;
    if (Loc) {
      line = Builder.getInt32(Loc.getLine());
      col = Builder.getInt32(Loc.getCol());
      file = Builder.CreateGlobalStringPtr(Loc->getFilename());
    } else {
      line = Builder.getInt32(-1);
      col = Builder.getInt32(-1);
      file = Builder.CreateGlobalStringPtr("unknown");
    }
    Builder.CreateCall(CheckFnTy, CheckFn, { caller, file, line, col, V });
  } else {
    Builder.CreateCall(CheckFnTy, CheckFn, {V});
  }

  NumCrossChecks++;
}

void DataChecks::InsertConditionChecks(LLVMContext &C) {
  // Eliminate duplicate values. We either insert checks directly after a value
  // is defined, or, when the value is not defined by an instruction, directly
  // before the branch that uses that value as a boolean. We want to coalesce
  // checks for the same value into a single check, but this can only be done
  // for values that are instructions. If the value to check is not defined by
  // an instruction, do not eliminate duplicates, since we will need to insert
  // multiple checks for that value (one before each use by a branch).
  //
  // The second case could be optimized if we find a common predecessor branch
  // for all branches that use a value and insert the check there, but... that
  // sounds complicated and I think that most values have instructions defining
  // them.
  if (!CheckAtBranch) {
    std::stable_sort(ConditionsToCheck.begin(), ConditionsToCheck.end(),
              [](const ConditionValue &a, const ConditionValue &b) {
                return a.V < b.V;
              });
    ConditionsToCheck.erase(std::unique(ConditionsToCheck.begin(), ConditionsToCheck.end(),
                                    [](const ConditionValue &a, const ConditionValue &b) {
                                      return a.V == b.V && isa<Instruction>(a.V) &&
                                        a.Location == DataChecks::Load &&
                                        b.Location == DataChecks::Load;
                                    }),
                        ConditionsToCheck.end());
  }

  IRBuilder<> Builder(C);
  for (auto &Condition : ConditionsToCheck) {
    auto *I = dyn_cast<Instruction>(Condition.V);
    if (Condition.Location == DataChecks::Load && I) {
      if (isa<PHINode>(I) || I->isEHPad()) {
        // Insert at the basic block's first insertion point
        Builder.SetInsertPoint(I->getParent(), I->getParent()->getFirstInsertionPt());
      } else {
        // Insert after the value
        Builder.SetInsertPoint(I->getNextNode());
      }
    } else {
      // Insert before the branch
      Builder.SetInsertPoint(Condition.U);
    }

    CreateCrossCheck(Builder, Condition.V);
  }
}

void DataChecks::DoControlFlowChecks(Module &M) {
  for (auto &F : M) {
    if (F.hasFnAttribute(Attribute::CrossCheck) &&
        F.hasAddressTaken() && !F.isDeclarationForLinker()) {
      IRBuilder<> Builder(M.getContext());
      Builder.SetInsertPoint(&*F.getEntryBlock().getFirstInsertionPt());
      CreateCrossCheck(Builder, Builder.getInt64(F.getGUID()));
    }
  }
}

namespace llvm {
  ModulePass *createDataChecksPass() { return new DataChecks(); }
}
