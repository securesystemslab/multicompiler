//===- DataRando.cpp - Data Randomization -----------------------*- C++ -*-===//

#ifndef LLVM_DATARANDO_DATARANDO_H
#define LLVM_DATARANDO_DATARANDO_H

#include "llvm/Pass.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/DataRando/Runtime/DataRandoTypes.h"
#include "dsa/DSGraph.h"
#include "dsa/DSNode.h"
#include "dsa/DSSupport.h"
#include "dsa/FormatFunctions.h"

namespace llvm {
class PointerEquivalenceAnalysis;
class SteensgaardsPEA;
class DataStructures;
class BUMarkDoNotEncrypt;
typedef ValueMap<const Value *, WeakVH> ValueToValueMapTy;

struct DataRando : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  DataRando() : ModulePass(ID) {}

  ValueMap<Value*, Value*> decryptedInstructions;

  bool runOnModule(Module &B) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

// Context Sensitive Data Randomization
struct CSDataRando : public ModulePass {
  struct FuncInfo {
    // The list of nodes which are passed masks as arguments
    std::vector<const DSNode*> ArgNodes;

    // The map of node to mask argument value
    DenseMap<const DSNode*, Value*> ArgMaskMap;

    // Map values in the new function to the values in the original function
    ValueMap<const Value*, const Value*> NewToOldMap;

    // Map values in the original function to the values in the cloned function
    ValueToValueMapTy OldToNewMap;

    // Map nodes containing globals to nodes in the globals graph
    DenseMap<const DSNode*, const DSNode*> ToGlobalNodeMap;

    bool CanReplaceAddress = true;
  };

  static char ID;
  CSDataRando() : ModulePass(ID) { }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool doFinalization(Module &) override;

private:
  Function *makeFunctionClone(Function *Original);

  void findGlobalNodes(Module &M);

  void findArgNodes(Module &M);

  void findFunctionArgNodes(const Function *F) {
    findFunctionArgNodes(std::vector<const Function*>({F}));
  }

  void findFunctionArgNodes(const std::vector<const Function *> &Functions);

  bool replaceWithClones(Function *F, FuncInfo &FI, PointerEquivalenceAnalysis &PEA, DSGraph *G);

  bool processCallSite(CallSite CS, FuncInfo &FI, PointerEquivalenceAnalysis &P, DSGraph *G);

  const Function *getEffectiveCallee(DSCallSite &DSCS, FuncInfo &FI, DSGraph *G);

  void getArgNodesForCall(DSGraph *CalleeGraph, DSCallSite DSCS, std::vector<DSNodeHandle> &ArgNodes);

  Value *getCloneCalledValue(CallSite CS, FuncInfo &CalleeInfo);

  bool replaceOriginalsWithClones();

  BUMarkDoNotEncrypt *DSA;
  MapVector<Function*, Function*> OldToNewFuncMap;
  std::map<const Function*, FuncInfo> FunctionInfo;
  DenseSet<const DSNode*> GlobalNodes;
  Type *MaskTy;
};

class FunctionWrappers : public ModulePass {
public:
  struct WrapperInfo {
    WrapperInfo(const char *N, const Type *T)
        : Name(N), Ty(T) {}

    const char *Name;
    const Type *Ty;
  };

private:
  StringMap<const WrapperInfo> Wrappers;
  DenseSet<const Type*> CantEncryptTypes;
  DenseSet<StringRef> MemManagement;
  DenseSet<StringRef> JmpFunctions;
  DenseSet<StringRef> RTTIVtables;
  FormatFunctions *FormatFuncs;

public:
  static char ID;
  FunctionWrappers() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequiredTransitive<FormatFunctions>();
  }

  void constructMap(LLVMContext &C);

  const StringMap<const WrapperInfo> *getWrappers() const {
    return &Wrappers;
  }

  bool hasWrapperFunction(const Function *F) const {
    const StringRef Name = Function::getRealLinkageName(F->getName());
    return Wrappers.count(Name);
  }

  bool typeCanBeEncrypted(const Type* T) const {
    if (CantEncryptTypes.count(T)) {
      return false;
    }

    const StructType *ST;
    if (T->isPointerTy()) {
      ST = dyn_cast<StructType>(T->getPointerElementType());
    } else {
      ST = dyn_cast<StructType>(T);
    }

    if (ST && ST->isOpaque()) {
      return false;
    }

    return true;
  }

  bool isMemManagementFunction(const Function *F) const {
    const StringRef Name = Function::getRealLinkageName(F->getName());
    return MemManagement.count(Name);
  }

  bool isFormatFunction(const Value *V) const {
    return FormatFuncs->isFormatFunction(V);
  }

  // For setjmp/longjmp, we can handle these even though they don't have wrapper
  // functions.
  bool isJmpFunction(const Function *F) const {
    const StringRef Name = Function::getRealLinkageName(F->getName());
    return JmpFunctions.count(Name);
  }

  bool isRTTIVtable(const GlobalVariable &G) const {
    return RTTIVtables.count(G.getName());
  }
};

}
#endif /* LLVM_DATARANDO_DATARANDO_H */
