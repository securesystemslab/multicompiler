//===- DataRando.cpp - Data Randomization ---------------------------------===//

#define DEBUG_TYPE "DataRando"

#include "llvm/DataRando/DataRando.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/DataRando/PointerEquivalenceAnalysis.h"
#include "llvm/DataRando/DataRandomizer.h"

#include "llvm/RangeValue/RangeValue.h"
#include "llvm/CrititcalValue/CriticalValue.h"

using namespace llvm;

STATISTIC(NumGlobals, "Number of global variables defined in module");
STATISTIC(NumUnencyptedGlobals, "Number of global variables defined in module which are not encrypted");

namespace {

bool maskIsNull(const Value *Mask) {
  if (const Constant *C = dyn_cast<Constant>(Mask)) {
    return C->isNullValue();
  }
  return false;
}

struct DataRandoVisitor : public InstVisitor<DataRandoVisitor> {

  ValueMap<Value*, Value*>* decryptedInstructions;

  explicit DataRandoVisitor(PointerEquivalenceAnalysis& A, Module &Mdl, ValueMap<Value*, Value*>* decryptedInsts)
      : decryptedInstructions(decryptedInsts), performedReplacement(false), PEA(A), DR(Mdl.getDataLayout(), Mdl.getContext()), M(Mdl) 
  {
  }

  void visitLoadInst(LoadInst &I) {
    Value *Mask = PEA.getMask(I.getPointerOperand());
    if (!maskIsNull(Mask)) {
      IRBuilder<> builder(&I);

      // Remove range metadata since this is broken with masking
      I.setMetadata(LLVMContext::MD_range, NULL);
      Value *myLoad = builder.Insert(I.clone());
      Value *xorValue = DR.createXor(builder, myLoad, I.getPointerOperand(), Mask, I.getAlignment());

      if (decryptedInstructions != NULL){
        (*decryptedInstructions)[myLoad] = xorValue;
      }

      I.replaceAllUsesWith(xorValue);
      PEA.replace(&I, xorValue);
      I.eraseFromParent();

      performedReplacement = true;
    }
  }

  void visitStoreInst(StoreInst &I) {
    Value *Mask = PEA.getMask(I.getPointerOperand());
    if (!maskIsNull(Mask)) {
      IRBuilder<> builder(&I);

      Value *xorValue = DR.createXor(builder, I.getValueOperand(), I.getPointerOperand(), Mask, I.getAlignment());
      I.replaceUsesOfWith(I.getValueOperand(), xorValue);

      performedReplacement = true;
    }
  }

  void visitVAArgInst(VAArgInst &I) {
    Value *Mask = PEA.getMask(I.getPointerOperand());
    if (!maskIsNull(Mask)) {
      IRBuilder<> builder(&I);

      // TODO: this is exactly the same as load
      Value *myVaarg = builder.Insert(I.clone());
      Value *xorValue = DR.createXor(builder, myVaarg, I.getPointerOperand(), Mask);

      I.replaceAllUsesWith(xorValue);
      PEA.replace(&I, xorValue);
      I.eraseFromParent();

      performedReplacement = true;
    }
  }

  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
    Value *Mask = PEA.getMask(I.getPointerOperand());
    if (!maskIsNull(Mask)) {
      // compare operand should be an unencrypted value, however the
      // data in memory it will be compared against will be encrypted,
      // need to xor both compare and new val
      IRBuilder<> builder(&I);

      Value *xorCompareOp = DR.createXor(builder, I.getCompareOperand(), I.getPointerOperand(), Mask);
      I.replaceUsesOfWith(I.getCompareOperand(), xorCompareOp);

      Value *xorNewValOp = DR.createXor(builder, I.getNewValOperand(), I.getPointerOperand(), Mask);
      I.replaceUsesOfWith(I.getNewValOperand(), xorNewValOp);

      performedReplacement = true;
    }
  }

  void visitAtomicRMWInst(AtomicRMWInst &I) {
    Value *Mask = PEA.getMask(I.getPointerOperand());
    if (!maskIsNull(Mask)) {
      IRBuilder<> builder(&I);

      switch (I.getOperation()) {
      case AtomicRMWInst::BinOp::Xchg:
        {
          // *p = v
          //
          // xor the value to be stored and xor the returned value of
          // this instruction
          Value *xorVal = DR.createXor(builder, I.getValOperand(), I.getPointerOperand(), Mask);
          Instruction *myRMW = builder.Insert(I.clone());
          myRMW->replaceUsesOfWith(I.getValOperand(), xorVal);
          Value *xorInst = DR.createXor(builder, myRMW, I.getPointerOperand(), Mask);
          I.replaceAllUsesWith(xorInst);
          PEA.replace(&I, xorInst);
          I.eraseFromParent();
        }
        break;
      case AtomicRMWInst::BinOp::Xor:
        {
          // *p = old ^ v
          //
          // old is encrypted, xor is malleable, so old ^ v will be
          // the correct encrypted value, only xor the returned value
          // of this instruction
          Instruction *myRMW = builder.Insert(I.clone());
          Value *xorInst = DR.createXor(builder, myRMW, I.getPointerOperand(), Mask);
          I.replaceAllUsesWith(xorInst);
          PEA.replace(&I, xorInst);
          I.eraseFromParent();
        }
        break;
      default:
        // TODO: handle other operations, may not be possible to do in
        // a single atomic operation. Alternative is to assign this
        // pointer equivalence class a 0 mask, but not good for
        // security.
        return;
      }

      performedReplacement = true;
    }
  }

  void visitMemSetInst(MemSetInst &I) {
    Value *Mask = PEA.getMask(I.getDest());
    if (!maskIsNull(Mask)) {
      // insert a call to drrt_xor_mem after the memset
      IRBuilder<> Builder(I.getNextNode());
      SmallVector<Value *, 8> args;
      LLVMContext &C = M.getContext();
      FunctionType *FT = TypeBuilder<void*(void*,int,size_t,mask_t,mask_t), false>::get(C);
      args.push_back(Builder.CreateBitCast(I.getDest(), TypeBuilder<void*, false>::get(C)));
      args.push_back(Builder.CreateZExtOrTrunc(I.getValue(), TypeBuilder<int, false>::get(C)));
      args.push_back(Builder.CreateZExtOrTrunc(I.getLength(), TypeBuilder<size_t, false>::get(C)));
      args.insert(args.end(), 2, Mask);
      Constant *F  = M.getOrInsertFunction("drrt_memset", FT);
      Builder.CreateCall(FT, F, args);
      I.eraseFromParent();
      performedReplacement = true;
    }
  }

  void visitMemTransferInst(MemTransferInst &I) {
    Value *SrcMask = PEA.getMask(I.getSource());
    Value *DestMask = PEA.getMask(I.getDest());
    LLVMContext &C = M.getContext();
    Type *MaskType = TypeBuilder<mask_t, false>::get(C);
    uint64_t MaskSize = M.getDataLayout().getTypeStoreSize(MaskType);

    // The analysis will always place source and destination in the same
    // equivalence class. We need to instrument this instruction if it is not
    // aligned to the size of the mask.
    assert(SrcMask == DestMask && "Source and destination not in the same class");
    if (!maskIsNull(SrcMask) &&
        (I.getAlignment() == 0 || I.getAlignment() % MaskSize)) {
      IRBuilder<> Builder(&I);
      SmallVector<Value *, 8> args;
      FunctionType *FT = TypeBuilder<void*(void *, const void *, size_t, mask_t, mask_t, mask_t), false>::get(C);
      Type *VoidPtrType = TypeBuilder<void*, false>::get(C);
      Type *SizeType = TypeBuilder<size_t, false>::get(C);
      Constant *F = M.getOrInsertFunction("drrt_memmove", FT);
      args.push_back(Builder.CreateBitCast(I.getDest(), VoidPtrType));
      args.push_back(Builder.CreateBitCast(I.getSource(), VoidPtrType));
      args.push_back(Builder.CreateZExtOrTrunc(I.getLength(), SizeType));
      args.push_back(DestMask);
      args.push_back(DestMask);
      args.push_back(SrcMask);
      // Create a call to the memmove wrapper instead
      Builder.CreateCall(FT, F, args);
      // We don't need to RAUW I since it doesn't return any value.
      I.eraseFromParent();
      performedReplacement = true;
    }
  }

  bool performedModification() {
    return performedReplacement;
  }


private:
  bool performedReplacement;
  PointerEquivalenceAnalysis &PEA;
  DataRandomizer DR;
  Module &M;
};

struct WrapLibraryFunctionsVisitor : public InstVisitor<WrapLibraryFunctionsVisitor> {
  WrapLibraryFunctionsVisitor(Module &M, PointerEquivalenceAnalysis &PEA, const FunctionWrappers &FW)
      : M(M), DL(M.getDataLayout()), Modified(false), FW(FW), PEA(PEA) {
  }

  Module &M;
  const DataLayout &DL;
  bool Modified;
  const FunctionWrappers &FW;
  PointerEquivalenceAnalysis &PEA;
  StringMap<bool> WrapperWarnedSet;

  void warnTypeConflict(StringRef Name, const Type *DeclaredType, const Type *Expected) {
    if (WrapperWarnedSet.insert(std::make_pair(Name, true)).second) {
      errs() << "Warning: ("<< Name << ") Declared type of wrapper does not match expected type\n";
      errs() << "\tDeclared type: ";
      DeclaredType->dump();
      errs() << "\tExpected type: ";
      Expected->dump();
    }
  }

  Constant *getWrapperFunction(Function *F) {
    StringRef Name = Function::getRealLinkageName(F->getName());
    auto Wrappers = FW.getWrappers();
    auto I = Wrappers->find(Name);
    if (I != Wrappers->end()) {
      FunctionType *FT = getWrapperTy(F->getFunctionType(), FW.isFormatFunction(F));
      DEBUG(
          if (FT != I->getValue().Ty) {
            warnTypeConflict(Name, I->getValue().Ty, FT);
          });
      return M.getOrInsertFunction(I->getValue().Name, FT);
    }

    return nullptr;
  }

  void visitCallSite(CallSite CS) {
    Function *CalledFun = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());

    // If it is an indirect call or the function is defined in this module, do
    // nothing. Right now we handle indirect calls by forcing the return value
    // and all arguments to indirect call sites to external functions to be unencrypted.
    // TODO: Handle wrapping indirect calls
    if (!(CalledFun && CalledFun->isDeclaration())) {
      return;
    }

    if (Constant *W = getWrapperFunction(CalledFun)) {
      SmallVector<Value*, 8> Args;
      collectArguments(Args, CS);
      FunctionType *FT = getWrapperTy(CS.getFunctionType(), FW.isFormatFunction(CalledFun));

      // We put a cast here just in case the original function was cast
      Constant* WrapperFun = ConstantExpr::getBitCast(W, PointerType::getUnqual(FT));

      Value *Call;
      if (CS.isCall()) {
        Call = CallInst::Create(FT, WrapperFun, Args, "", CS.getInstruction());
      } else {
        InvokeInst *II = cast<InvokeInst>(CS.getInstruction());
        Call = InvokeInst::Create(FT, WrapperFun, II->getNormalDest(), II->getUnwindDest(), Args, "", II);
      }
      CallSite NewCS(Call);
      NewCS.setCallingConv(CS.getCallingConv());
      CS.getInstruction()->replaceAllUsesWith(Call);
      PEA.replace(CS.getInstruction(), Call);
      CS.getInstruction()->eraseFromParent();
      Modified = true;
    } else if (FW.isJmpFunction(CalledFun)) {
      // Handle setjmp/longjmp. We decrypt the jmp_buf before each of these
      // functions, and re-encrypt it afterwards. We cannot use a wrapper
      // function to do this since if setjmp is called within a wrapper
      // function, when longjmp is called it would be trying to return to a
      // stack frame that no longer exists since the wrapper would have
      // returned.
      if (CS.getNumArgOperands() < 1 || CalledFun->getFunctionType()->getNumParams() < 1) {
        DEBUG(errs() << "Suspicious call to " << CalledFun->getName() << '\n');
        return;
      }

      // The first argument to both setjmp and longjmp is the jmp_buf.
      Value *JmpBuf = CS.getArgOperand(0);
      Value *Mask = PEA.getMask(JmpBuf);

      // Determine the size of the jmp_buf from the type of the argument to the
      // function. On x86 Linux jmp_buf is a typedef of an array type, so it
      // will be passed as a pointer. Use the size of the pointer element type
      // to determine the size of jmp_buf.
      Type *JmpBufPtrTy = CalledFun->getFunctionType()->getParamType(0);
      if (!JmpBufPtrTy->isPointerTy()) {
        DEBUG(errs() << "Suspicious call to " << CalledFun->getName() << '\n');
        return;
      }

      Type *JmpBufTy = JmpBufPtrTy->getPointerElementType();

      if (!JmpBufTy->isSized()) {
        DEBUG(errs() << "Suspicious call to " << CalledFun->getName() << '\n');
        return;
      }

      uint64_t JmpBufSize = DL.getTypeAllocSize(JmpBufTy);

      auto &Context = CS.getInstruction()->getContext();
      FunctionType *FT = TypeBuilder<void(void*,mask_t,size_t), false>::get(Context);
      Constant *Encrypt = M.getOrInsertFunction("drrt_xor_mem", FT);

      IRBuilder<> Builder(CS.getInstruction());

      // Prepare the argument list
      SmallVector<Value *, 4> args;
      args.push_back(Builder.CreateBitCast(JmpBuf, TypeBuilder<void*, false>::get(Context)));
      args.push_back(Mask);
      args.push_back(ConstantInt::get(TypeBuilder<size_t, false>::get(Context), JmpBufSize));

      // Surround the call site with calls to drrt_xor_mem.
      Builder.CreateCall(FT, Encrypt, args);
      auto it = CS.getInstruction()->getIterator();
      it++;
      Builder.SetInsertPoint(CS.getInstruction()->getParent(), it);
      Builder.CreateCall(FT, Encrypt, args);
    }
  }

  unsigned numberOfMasksNeeded(Type *T) {
    DenseSet<StructType*> DS;
    return numberOfMasksNeeded(T, DS);
  }

  unsigned numberOfMasksNeeded(Type *T, DenseSet<StructType*> &Visited) {
    if (! FW.typeCanBeEncrypted(T)) {
      return 0;
    }
    unsigned Count = 0;
    if (T->isPointerTy() && !T->getPointerElementType()->isFunctionTy()) {
      StructType *ST = dyn_cast<StructType>(T->getPointerElementType());
      if (!ST || !Visited.count(ST)) {
        // mask for this pointer and things reachable from this pointer
        ++Count;
        Count += numberOfMasksNeeded(T->getPointerElementType(), Visited);
      }
    } else if (StructType *ST = dyn_cast<StructType>(T)) {
      Visited.insert(ST);
      for (Type::subtype_iterator i = ST->element_begin(), e = ST->element_end(); i != e; ++i) {
        Count += numberOfMasksNeeded(*i, Visited);
      }

    }
    return Count;
  }

  FunctionType *getWrapperTy(FunctionType *FT, bool FormatFunction) {
    Type *Mask_t = TypeBuilder<mask_t, false>::get(FT->getContext());
    SmallVector<Type*, 8> ParamTys;
    unsigned Masks = 0;

    Masks += numberOfMasksNeeded(FT->getReturnType());

    for (unsigned i = 0, n = FT->getNumParams(); i < n; i++) {
      Type *T = FT->getParamType(i);
      ParamTys.push_back(T);
      Masks += numberOfMasksNeeded(T);
    }

    if (FT->isVarArg() && !FormatFunction) {
      Masks += 1;
    }

    // Append masks to the end of the function's parameter list
    for (unsigned i = 0; i < Masks; i++) {
      ParamTys.push_back(Mask_t);
    }

    return FunctionType::get(FT->getReturnType(), ParamTys, FT->isVarArg());
  }

  void collectArguments(SmallVectorImpl<Value*> &Args, CallSite CS) {
    Args.clear();
    // NumParams will be the number of regular arguments, not including varargs
    unsigned NumParams = CS.getFunctionType()->getNumParams();
    SmallVector<Value*, 8> Masks;
    bool FormatFunction = FW.isFormatFunction(CS.getCalledValue());

    // Find mask for return type
    PEA.appendMasksForReachable(CS.getInstruction(), DL, FW, Masks);

    // Normal args go first
    for (unsigned i = 0; i < NumParams; ++i) {
      Value *A = CS.getArgument(i);
      Args.push_back(A);

      // Get masks for arguments in order, we will only pass masks for regular
      // arguments, not vararg arguments
      PEA.appendMasksForReachable(A, DL, FW, Masks);
    }

    if (!FormatFunction) {
      PEA.appendMaskForVarArgs(CS, Masks);
    }

    // Then masks
    Args.append(Masks.begin(), Masks.end());

    // Followed by varargs
    for (unsigned i = NumParams, n = CS.arg_size(); i < n; ++i) {
      Value *A = CS.getArgument(i);
      Args.push_back(A);
      if (FormatFunction) {
        Args.push_back(PEA.getMask(A));
      }
    }
  }
};
}

bool DataRando::runOnModule(Module &M) {
  SteensgaardsPEA &PEA = getAnalysis<SteensgaardsPEA>();
  FunctionWrappers &FW = getAnalysis<FunctionWrappers>();
  DataRandomizer DR(M);
  bool Modified = DR.instrumentMemoryOperations(M, PEA, &decryptedInstructions);
  Modified |= DR.encryptMainArgs(M, PEA, FW);
  Modified |= DR.encryptGlobalVariables(M, PEA);
  Modified |= DR.wrapLibraryFunctions(M, PEA, FW);

  // Equivalence classes will be printed after all transformations, so the
  // output will represent the results of all replacements.
  if (!PointerEquivalenceAnalysis::PrintEquivalenceClassesTo.empty()) {
    PEA.printEquivalenceClasses(PointerEquivalenceAnalysis::PrintEquivalenceClassesTo, M);
  }
  if (PointerEquivalenceAnalysis::PrintAllocationCounts) {
    PEA.printAllocationCounts();
  }
  return Modified;
}

void DataRando::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<SteensgaardsPEA>();
  // nothing in this pass will change the equivalence classes
  AU.addPreserved<SteensgaardsPEA>();
  AU.addRequired<FunctionWrappers>();
  AU.addPreserved<FunctionWrappers>();
}

bool DataRandomizer::instrumentMemoryOperations(Module &M, PointerEquivalenceAnalysis &PEA, ValueMap<Value*, Value*>* decryptedInstructions) {
  DataRandoVisitor v(PEA, M, decryptedInstructions);
  v.visit(M);
  return v.performedModification();
}

bool DataRandomizer::instrumentMemoryOperations(Function &F, PointerEquivalenceAnalysis &PEA, ValueMap<Value*, Value*>* decryptedInstructions) {
  DataRandoVisitor v(PEA, *F.getParent(), decryptedInstructions);
  v.visit(F);
  return v.performedModification();
}

bool DataRandomizer::encryptMainArgs(Module &M, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW) {
  if (Function *Main = M.getFunction("main")) {
    if (Main->arg_size() == 0) {
      return false;
    }
    assert((Main->arg_size() == 2 || Main->arg_size() == 3) && "Main does not have correct number of arguments");

    FunctionType *FT = TypeBuilder<int(int,char**,mask_t,mask_t), false>::get(M.getContext());

    BasicBlock &B = Main->getEntryBlock();
    IRBuilder<> Builder(&B, B.begin());
    Constant *Wrapper = M.getOrInsertFunction("drrt_main", FT);
    SmallVector<Value *, 4> args;
    auto MainArgI = Main->arg_begin();

    // We will only encrypt the first two arguments
    args.push_back(&*MainArgI);
    MainArgI++;
    args.push_back(&*MainArgI);

    // get the second argument to Main
    auto A = Main->arg_begin();
    A++;
    PEA.appendMasksForReachable(&*A, M.getDataLayout(), FW, args);

    Builder.CreateCall(FT, Wrapper, args);
    return true;
  }
  return false;
}

bool DataRandomizer::encryptGlobalVariables(Module &M, PointerEquivalenceAnalysis &PEA) {
  // define a function to encrypt global variables in this module

  if (M.getGlobalList().empty()) {
    return false;
  }

  LLVMContext &Context = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  DataRandomizer DR(DL, M.getContext());
  FunctionType *CtorFunTy = FunctionType::get(Type::getVoidTy(Context), false);
  Function *F = Function::Create(CtorFunTy, GlobalValue::LinkageTypes::InternalLinkage, "drrt_encrypt_globals", &M);
  BasicBlock *B = BasicBlock::Create(Context, "entry", F);
  IRBuilder<> Builder(B);
  for (GlobalVariable &G : M.getGlobalList()) {
    if (PointerEquivalenceAnalysis::shouldIgnoreGlobal(G) || G.getName().equals("llvm.global_ctors")) {
      continue;
    }

    // it is unnecessary to encrypt global variables that don't have an
    // initializer, this will also prevent encrypting global variables that are
    // defined outside of this compilation unit
    if (! G.hasInitializer()) {
      continue;
    }

    NumGlobals++;

    Value *Mask = PEA.getMask(&G);
    if (maskIsNull(Mask)) {
      NumUnencyptedGlobals++;
      continue;
    }

    Type *ElementTy = G.getType()->getElementType();

    // Don't try to encrypt trampolines, even if their target equivalence class
    // is a valid "encryptable" function. We use a DF iterator to simplify
    // chasing down ArrayTy->PointerTy->TrampolineTy.
    bool isTrampoline = false;
    for (auto *ChildTy : depth_first(ElementTy))
      if (ChildTy->isTrampolineTy()) {
        isTrampoline = true;
        break;
      }
    if (isTrampoline)
      continue;

    // set all values as writable
    // TODO: Change the constant initializer instead
    G.setConstant(false);

    if(ElementTy->isArrayTy() || ElementTy->isStructTy()) {
      FunctionType *FT = TypeBuilder<void(void*,mask_t,size_t), false>::get(Context);
      SmallVector<Value *, 4> args;
      args.push_back(Builder.CreateBitCast(&G, TypeBuilder<void*, false>::get(Context)));
      args.push_back(Mask);
      args.push_back(ConstantInt::get(TypeBuilder<size_t, false>::get(Context), DL.getTypeAllocSize(ElementTy)));
      Constant *Encrypt = M.getOrInsertFunction("drrt_xor_mem", FT);
      Builder.CreateCall(FT, Encrypt, args);
    } else {
      Value *Load = Builder.CreateAlignedLoad(&G, G.getAlignment());
      Value *Xor = DR.createXor(Builder, Load, &G, Mask, G.getAlignment());
      Builder.CreateStore(Xor, &G);
    }
  }

  // terminate the function
  Builder.CreateRetVoid();

  // add this function to llvm.global_ctors so it will be ran before any other
  // code in this module. We add the function with priority 2 so that it will
  // run after the vtable_rando initializer, which has priority 1. This will
  // break other initializers at priority 0, 1, and 2 that assume global data is
  // already encrypted.  TODO: Shift all constructors up to priority 3 other
  // than the vtable_rando initializer.

  // CtorStructTy = { i32, void ()*, i8* }
  StructType *CtorStructTy = StructType::get(
      Type::getInt32Ty(Context),
      PointerType::getUnqual(CtorFunTy),
      Type::getInt8PtrTy(Context),
      nullptr);

  // create vector of Ctors
  SmallVector<Constant *, 4> Ctors;
  Constant *S[] = { ConstantInt::get(Type::getInt32Ty(Context), 2), F, Constant::getNullValue(Type::getInt8PtrTy(Context)) };
  Ctors.push_back(ConstantStruct::get(CtorStructTy, S));

  if (GlobalVariable *G = M.getGlobalVariable("llvm.global_ctors")) {
    // collect the existing Ctors
    if (ConstantArray *CA = dyn_cast_or_null<ConstantArray>(G->getInitializer())) {
      for (unsigned i = 0, n = CA->getNumOperands(); i < n; ++i) {
        Ctors.push_back(CA->getOperand(i));
      }
    }

    // erase, we will then insert a new global
    G->eraseFromParent();
  }

  ArrayType *AT = ArrayType::get(CtorStructTy, Ctors.size());
  new GlobalVariable(M, AT, false, GlobalValue::AppendingLinkage, ConstantArray::get(AT, Ctors), "llvm.global_ctors");
  return true;
}

bool DataRandomizer::wrapLibraryFunctions(Module &M, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW) {
  WrapLibraryFunctionsVisitor V(M, PEA, FW);
  V.visit(M);
  return V.Modified;
}

bool DataRandomizer::wrapLibraryFunctions(Function &F, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW) {
  WrapLibraryFunctionsVisitor V(*F.getParent(), PEA, FW);
  V.visit(F);
  return V.Modified;
}

char DataRando::ID = 0;
static RegisterPass<DataRando> X("data-rando", "Data randomization pass");
