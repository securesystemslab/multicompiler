//===- PointerEquivalenceAnalysis.h - Build equivalence classes -*- C++ -*-===//

#ifndef LLVM_DATARANDO_POINTEREQUIVALENCEANALYSIS_H
#define LLVM_DATARANDO_POINTEREQUIVALENCEANALYSIS_H

#include "llvm/Pass.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/DataRando/Runtime/DataRandoTypes.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/ADT/StringSet.h"
#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"
#include <vector>

namespace llvm {
class AliasSetTracker;
class AliasSet;
class FunctionWrappers;

class PointerEquivalenceAnalysis {
public:
  static cl::opt<unsigned int> EffectiveMaskSize;
  static cl::opt<std::string> PrintEquivalenceClassesTo;
  static cl::opt<bool> PrintAllocationCounts;

  // A simple node handle that stores the node and the offset. This doesn't
  // do any of the forwarding and reference counting stuff that caused issues
  // when putting DSNodeHandles in a ValueMap.
  class NodeHandle {
  public:

    const DSNode * Node;
    unsigned Offset;

    NodeHandle() : Node(nullptr), Offset(0) {}

    NodeHandle(const DSNodeHandle &NH) {
      Node = NH.getNode();
      Offset = NH.getOffset();
    }

    NodeHandle(const DSNode* N, unsigned Offset = 0) : Node(N), Offset(Offset) {}

    const DSNode* getNode() const {
      return Node;
    }

    unsigned int getOffset() const {
      return Offset;
    }

    NodeHandle getLink(unsigned Num) const {
      if (Node) {
        unsigned EffectiveOffset = Node->isNodeCompletelyFolded() ? 0 : Offset + Num;
        if (EffectiveOffset < Node->getSize() && Node->hasLink(EffectiveOffset)) {
          return NodeHandle(Node->getLink(EffectiveOffset));
        }
      }
      return NodeHandle();
    }
  };


  Value *getMask(const Value *V) {
    return getMaskForNode(getNode(V));
  }

  void appendMasksForReachable(const Value *V, const DataLayout &DL, const FunctionWrappers &FW, SmallVectorImpl<Value *> &SV);

  void appendMaskForVarArgs(ImmutableCallSite CS, SmallVectorImpl<Value *> &SV);

  // If a value is RAUWed call this function to update the analysis. The default
  // implementation is a nop.
  virtual void replace(const Value *Old, const Value *New) {
  }

  virtual NodeHandle getNode(const Value *V) = 0;
  virtual Value *getMaskForNode(const NodeHandle &NH) = 0;

  static bool shouldIgnoreGlobal(const GlobalVariable &GV) {
    // This is the same condition that the local DSA pass uses to determine if a
    // global should be ignored.
    return GV.hasSection() && (StringRef(GV.getSection()) == "llvm.metadata");
  }

  static void printClass(raw_ostream &S, const DSNode* N, Value *Mask, const StringSet<> &Reasons, std::vector<const Value*> &ValueList);

protected:
  Constant *nextMask() {
    assert(RNG && "RNG not initialized, call init first.");
    mask_t M;
    do {
      M = RNG->Random();
      mask_t BitsNeededMask = ((uint64_t)-1) >> ((sizeof(mask_t) - EffectiveMaskSize) * 8);
      // Get the bits we need
      M &= BitsNeededMask;
      // Fill the rest of the mask with the repeated value;
      for (unsigned i = EffectiveMaskSize; i < sizeof(mask_t); i *= 2) {
        M |= M << (8 * i);
      }
    } while (M == 0);
    return ConstantInt::get(getMaskTy(), M);
  }

  Constant *nullMask() {
    return Constant::getNullValue(getMaskTy());
  }

  IntegerType *getMaskTy() {
    assert(MaskTy && "MaskTy not initialized, call init first.");
    return MaskTy;
  }

  // Initialize the random number generator and mask type.
  void init(RandomNumberGenerator &R, LLVMContext &C);

private:
  RandomNumberGenerator *RNG;
  IntegerType *MaskTy;

  void appendMasksForReachable(Type *T, const NodeHandle &N, const DataLayout &DL, const FunctionWrappers &FW, SmallVectorImpl<Value *> &SV, DenseSet<StructType*> &Visited);
};

class SteensgaardsPEA : public PointerEquivalenceAnalysis, public ModulePass {

public:

  static char ID; // Pass identification, replacement for typeid
  SteensgaardsPEA();

  virtual bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  // Print all equivalence classes to FileName. This may take a lot of time if
  // the program is large. Also if Values are RAUWed after outputing the values
  // actually in the equivalence class will change.
  bool printEquivalenceClasses(StringRef FileName, const Module &M);

  bool printAllocationCounts();

  bool doFinalization(Module &) override;

  void *getAdjustedAnalysisPointer(const void *ID) override;

  NodeHandle getNode(const Value *V) override;

  Value *getMaskForNode(const NodeHandle &N) override;

private:
  // Add mappings from the DSGraph into the ValueMap NodeMap so that the mapping
  // from Value* to DSNode* will be maintained if a value is RAUWed. The
  // implementation of DSScalarMap uses a std::map for this, which will not
  // update if a value is RAUWed. There is some issue that I don't fully
  // understand with the DSNode reference counts if DSNodeHandle objects are
  // inserted into a DenseMap which causes an assertion failure when freeing the
  // memory from the DSGraphs. Since the analysis has completed, the identity of
  // the node referenced by the DSNodeHandle should not change, so it should be
  // safe to store the mapping from Value* to DSNode* in NodeMap. If NodeMap
  // already contains any entrys for Values in the DSGraph, those entries will
  // be overwritten.
  void addMappingsFromGraph(const DSGraph *DSG);

  void assignMask(const NodeHandle &N, Constant *M, StringRef Reason = StringRef());

  void assignMaskRecursively(const NodeHandle &N, Constant *M, DenseSet<const DSNode*> &Visited, StringRef Reason);

  void assignMaskRecursively(const NodeHandle &N, Constant *M, StringRef Reason = StringRef()) {
    DenseSet<const DSNode*> V;
    assignMaskRecursively(N, M, V, Reason);
  }

  void handleUnwrappedExternalCalls(const std::vector<ImmutableCallSite> &ExternalCalls);

  // Visit the call site of an unwrapped external function and set the mask to 0
  // for any of the return or argument values that have nodes in the DSGraph.
  // Return true if values were found that had nodes in the DSGraph.
  bool examineExternalCallSite(ImmutableCallSite CS);

  void safetyAnalysis();

  void warnUnknown(const DSNode *Node);

  ValueMap<const Value*, NodeHandle> NodeMap;
  DenseMap<const DSNode*, Constant*> MaskMap;
  DenseMap<const DSNode*, size_t> AccessCounts;
  const DataLayout *DL;
  LLVMContext *CTX;
  const FunctionWrappers *FW;
  DenseSet<const DSNode*> UsedUnknownNodes;
  DenseMap<const DSNode*, StringSet<>> MaskReason;
};
}

#endif /* LLVM_DATARANDO_POINTEREQUIVALENCEANALYSIS_H */
