//===- CSDataRando.cpp - Context sensitive Data Randomization -------------===//

#define DEBUG_TYPE "DataRando"

#include "llvm/DataRando/DataRando.h"
#include "llvm/DataRando/DataRandomizer.h"
#include "llvm/DataRando/PointerEquivalenceAnalysis.h"
#include "llvm/DataRando/MarkDoNotEncrypt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"

using namespace llvm;

STATISTIC(NumClones, "Number of functions with mask arguments added");
STATISTIC(NumClasses, "Number of equivalence classes assigned masks");
STATISTIC(NumMasks, "Number of random masks assigned to equivalence classes");
STATISTIC(NumHeap, "Number of heap equivalence classes");
STATISTIC(NumNotEncrypted, "Number of equivalence classes assigned null mask");
STATISTIC(NumFunctionECs, "Number of function equivalence classes");
STATISTIC(NumFunECsWithExternal, "Number of function equivalence classes containing external functions");
STATISTIC(NumGlobalECs, "Number of equivalence classes containing global variables");
STATISTIC(MaxSizeGlobalEC, "Maximum number of globals contained in a single equivalence class");
STATISTIC(NumIndirectCalls, "Number of indirect calls examined");
STATISTIC(NumIndirectCantEncrypt, "Number of indirect calls that could not be encrypted");

namespace {

struct CloneFunctionPEA;

struct ContextSensitivePEA : public PointerEquivalenceAnalysis {
  DenseMap<const DSNode*, Value*> MaskMap;
  ValueMap<const Value*, bool> MemberMap;
  CSDataRando::FuncInfo &Info;
  DSGraph &G;
  ContextSensitivePEA *GlobalPEA;
  bool TrackStatistics;

  ContextSensitivePEA(RandomNumberGenerator &R, LLVMContext &C, CSDataRando::FuncInfo &FI, DSGraph &G,
                      ContextSensitivePEA *GlobalPEA = nullptr, bool TrackStatistics = true)
      : Info(FI), G(G), GlobalPEA(GlobalPEA), TrackStatistics(TrackStatistics) {
    init(R, C);
  }

  virtual NodeHandle getNode(const Value *V) override {
    // The nodes for some global variables may have been deleted from the
    // function's DSGraph, so we first try to find a node in the GlobalPEA. If
    // that returns a non-null node then the value V must be a global variable
    // and we use that node.
    if (GlobalPEA) {
      NodeHandle NH = GlobalPEA->getNode(V);
      if (const DSNode * N = NH.getNode()) {
        // getMaskForNode will not know to use GlobalPEA to find the mask unless
        // this node appears in Info.ToGlobalMap so we map the node to itself.
        Info.ToGlobalNodeMap[N] = N;
        return N;
      }
    }
    DSNodeHandle NH = G.getNodeForValue(V);
    if (!NH.isNull()) {
      MemberMap[V] = true;
    }
    return NH;
  }

  virtual Value *getMaskForNode(const NodeHandle &NH) override {
    const DSNode *N = NH.getNode();
    if (N) {
      auto gbl = Info.ToGlobalNodeMap.find(N);
      if (gbl != Info.ToGlobalNodeMap.end()) {
        // TODO: if we remove global nodes from possible arg nodes we can remove this check.
        if (! Info.ArgMaskMap.count(N)) {
          assert(GlobalPEA && "Node found which maps to global nodes, but no global variable PEA available");
          return GlobalPEA->getMaskForNode(gbl->second);
        }
      }

      auto i = MaskMap.find(N);
      if (i != MaskMap.end()) {
        return i->second;
      }

      // If we cannot encrypt this node assign it the null mask, otherwise assign
      // it a random mask.
      Value *Mask;
      if (N->isDoNotEncryptNode()) {
        if (TrackStatistics) {
          NumNotEncrypted++;
        }
        Mask = nullMask();
      } else {
        if (TrackStatistics) {
          NumMasks++;
        }
        Mask = nextMask();
      }
      if (N->isHeapNode() && TrackStatistics) {
        NumHeap++;
      }
      if (TrackStatistics && PrintAllocationCounts) {
        errs() << "Node" << N << ',' << N->numAllocations() << ',' << *Mask << '\n';
      }
      MaskMap.insert(std::make_pair(N, Mask));
      return Mask;
    }
    return nextMask();
  }

  virtual void printEquivalenceClasses(raw_ostream &S) {
    printGtoGGMapping(S);

    DenseMap<const DSNode*, std::vector<const Value*> > ValueLists;
    for (auto I : MemberMap) {
      if (DSNode *N = G.getNodeForValue(I.first).getNode()) {
        ValueLists[N].push_back(I.first);
      }
    }

    for (auto I : ValueLists) {
      printClass(S, I.first, MaskMap[I.first], I.first->getReasons(), I.second);
    }
  }

  void printGtoGGMapping(raw_ostream &S) {
    if (Info.ToGlobalNodeMap.empty()) {
      return;
    }
    S << "Local to global node mappings:\n";

    for (auto I : Info.ToGlobalNodeMap) {
      // Only print mappings to different nodes, not mappings to the same node
      // which use a node from the global graph in the current
      // function.
      if (I.first != I.second) {
        S << "Node" << (const void*) I.first << " -> Node" << (const void*) I.second << '\n';
      }
    }
    S << '\n';
  }
};

struct FunctionPEA : public ContextSensitivePEA {

  FunctionPEA(RandomNumberGenerator &R, LLVMContext &C, CSDataRando::FuncInfo &FI, DSGraph &G, ContextSensitivePEA *GlobalPEA, bool TrackStatistics)
      : ContextSensitivePEA(R,C,FI,G,GlobalPEA, TrackStatistics) {
    for (const DSNode *N : Info.ArgNodes) {
      MaskMap[N] = nullMask();
    }
  }

  virtual void replace(const Value *Old, const Value *New) override {
    if (G.hasNodeForValue(Old)) {
      G.getScalarMap().replaceScalar(Old, New);
    }
  }

  virtual void printEquivalenceClasses(raw_ostream &S) {
    printGtoGGMapping(S);

    DenseMap<const DSNode*, std::vector<const Value*> > ValueLists;
    for (auto I : MemberMap) {
      if (DSNode *N = G.getNodeForValue(I.first).getNode()) {
        ValueLists[N].push_back(I.first);
      }
    }

    for (auto I : ValueLists) {
      if (Info.ArgMaskMap.count(I.first)) {
        StringSet<> Reasons;
        Reasons.insert("Argument node of original function");
        printClass(S, I.first, MaskMap[I.first], Reasons, I.second);
      } else {
        printClass(S, I.first, MaskMap[I.first], I.first->getReasons(), I.second);
      }
    }
  }
};

struct CloneFunctionPEA : public ContextSensitivePEA {

  CloneFunctionPEA(RandomNumberGenerator &R, LLVMContext &C, CSDataRando::FuncInfo &I, DSGraph &G, ContextSensitivePEA *GlobalPEA)
      : ContextSensitivePEA(R,C,I,G,GlobalPEA) {
    MaskMap.insert(I.ArgMaskMap.begin(), I.ArgMaskMap.end());
  }

  virtual NodeHandle getNode(const Value *V) override {
    const Value *OldV = Info.NewToOldMap[V];
    if (OldV != nullptr) {
      return ContextSensitivePEA::getNode(OldV);
    }
    return NodeHandle();
  }

  // This implementation of printEquivalenceClasses handles mapping values in
  // the original function to the values in the cloned function so that the
  // diagnostic information will be as clear as possible.
  virtual void printEquivalenceClasses(raw_ostream &S) override {
    printGtoGGMapping(S);

    DenseMap<const DSNode*, std::vector<const Value*> > ValueLists;
    for (auto I : MemberMap) {
      if (DSNode *N = G.getNodeForValue(I.first).getNode()) {
        Value *V = Info.OldToNewMap[I.first];
        ValueLists[N].push_back(V);
      }
    }

    for (auto I : ValueLists) {
      printClass(S, I.first, MaskMap[I.first], I.first->getReasons(), I.second);
    }
  }
};
}

// Get all nodes in all function DSGraphs and the global DSGraph that contain
// global values.
void CSDataRando::findGlobalNodes(Module &M) {
  DSGraph *GG = DSA->getGlobalsGraph();
  for (auto i = GG->node_begin(), e = GG->node_end(); i != e; i++) {
    GlobalNodes.insert(&*i);
  }

  for (Function &F : M) {
    if ((!F.isDeclaration()) && DSA->hasDSGraph(F)) {
      DSGraph *G = DSA->getDSGraph(F);
      FuncInfo &FI = FunctionInfo[&F];
      DSGraph::NodeMapTy NodeMap;
      G->computeGToGGMapping(NodeMap);
      for (auto i : NodeMap) {
        GlobalNodes.insert(i.first);
        FI.ToGlobalNodeMap[i.first] = i.second.getNode();
      }
    }
  }
}

namespace {
struct FindCallSiteVisitor : public InstVisitor<FindCallSiteVisitor> {
  std::vector<CallSite> CallSites;

  void visitCallSite(CallSite CS) {
    if (CS.isInlineAsm()) {
      return;
    }
    if (Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
      if (F->isDeclaration()) {
        return;
      }
    }
    CallSites.push_back(CS);
  }
};

const Value *originalValue(const CSDataRando::FuncInfo &FI, const Value *V) {
  auto i = FI.NewToOldMap.find(V);
  if (i == FI.NewToOldMap.end()) {
    return V;
  }
  return i->second;
}

CallSite originalCallSite(const CSDataRando::FuncInfo &FI, ImmutableCallSite CS) {
  // This const_cast here really sucks, but we need a CallSite for some
  // DSCallGraph methods like callee_begin.
  return CallSite(const_cast<Value*>(originalValue(FI, CS.getInstruction())));
}

}

const Function *CSDataRando::getEffectiveCallee(DSCallSite &DSCS, FuncInfo &FI, DSGraph *G) {
  // Direct calls to external functions are already filtered out by
  // FindCallSiteVisitor.
  if (DSCS.isDirectCall()) {
    return DSCS.getCalleeFunc();
  }

  // Handle indirect calls. Find a function in the equivalence class of
  // functions callable from this CallSite.
  const DSCallGraph &CG = DSA->getCallGraph();
  CallSite OriginalCS = DSCS.getCallSite();

  if (CG.callee_begin(OriginalCS) != CG.callee_end(OriginalCS)) {
    return *CG.callee_begin(OriginalCS);
  }

  return nullptr;
}

bool CSDataRando::replaceWithClones(Function *F, FuncInfo &FI, PointerEquivalenceAnalysis &PEA, DSGraph *G) {
  FindCallSiteVisitor Visitor;
  Visitor.visit(F);
  for (CallSite CS : Visitor.CallSites) {
    processCallSite(CS, FI, PEA, G);
  }
  return true;
}

// Get the value we should change this callsite to call instead.
Value *CSDataRando::getCloneCalledValue(CallSite CS, FuncInfo &CalleeInfo) {
  if (CalleeInfo.ArgNodes.size() == 0) {
    return nullptr;
  }

  // Find the function type we want based on how many args need to be added. We
  // do this in case the original function has been cast to a different type.
  FunctionType *FT = CS.getFunctionType();
  SmallVector<Type*, 8> Params;
  Params.insert(Params.end(), FT->param_begin(), FT->param_end());
  Params.insert(Params.end(), CalleeInfo.ArgNodes.size(), MaskTy);
  FunctionType *TargetType = FunctionType::get(FT->getReturnType(), Params, FT->isVarArg());

  IRBuilder<> Builder(CS.getInstruction());

  // Direct call, find the clone and cast it to what we want.
  if (Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
    Value *Clone = OldToNewFuncMap[F];
    if (Clone) {
      Clone = Builder.CreateBitCast(Clone, PointerType::getUnqual(TargetType));
    }
    return Clone;
  }

  // Indirect calls, cast the called value to the type we want.
  Value *CalledValue = CS.getCalledValue();
  return Builder.CreateBitCast(CalledValue, PointerType::getUnqual(TargetType));
}

// Get the arg nodes in CalleeGraph for the callee of DSCS. Like
// DSGraph::getFunctionArgumentsForCall, but for indirect calls gets the most
// non-null nodes from all functions callable from that callsite.
void CSDataRando::getArgNodesForCall(DSGraph *CalleeGraph, DSCallSite DSCS, std::vector<DSNodeHandle> &ArgNodes) {
  ArgNodes.clear();

  if (DSCS.isDirectCall()) {
    CalleeGraph->getFunctionArgumentsForCall(DSCS.getCalleeFunc(), ArgNodes);
    return;
  }

  // Handle indirect calls.
  const DSCallGraph &CG = DSA->getCallGraph();
  CallSite OriginalCS = DSCS.getCallSite();
  std::vector<DSNodeHandle> TempArgNodes;

  // Get as many non-null arg nodes as possible. We don't know what the actual
  // caller will be, and there can be some mismatch between which arguments have
  // nodes for the different functions which may be called from this callsite.
  // TODO: It should be possible to cache the result of this to use for all
  // calls within the function equivalence class.
  for (auto i = CG.callee_begin(OriginalCS), e = CG.callee_end(OriginalCS); i != e; i++) {
    TempArgNodes.clear();
    CalleeGraph->getFunctionArgumentsForCall(*i, TempArgNodes);
    for (unsigned int i = 0, e = TempArgNodes.size(); i < e; i++) {
      if (i < ArgNodes.size()) {
        if (ArgNodes[i].isNull() && (!TempArgNodes[i].isNull())) {
          ArgNodes[i] = TempArgNodes[i];
        }
      } else {
        ArgNodes.push_back(TempArgNodes[i]);
      }
    }
  }
}

bool CSDataRando::processCallSite(CallSite CS, FuncInfo &FI, PointerEquivalenceAnalysis &P, DSGraph *G) {
  bool IndirectCall = !isa<Function>(CS.getCalledValue()->stripPointerCasts());
  if (IndirectCall) { NumIndirectCalls++; }

  CallSite OriginalCS = originalCallSite(FI, CS);
  if (!DSA->canEncryptCall(OriginalCS)) {
    if (IndirectCall) { NumIndirectCantEncrypt++; }
    return false;
  }

  DSCallSite DSCS = G->getDSCallSiteForCallSite(OriginalCS);
  const Function *Callee = getEffectiveCallee(DSCS, FI, G);
  if (!Callee) {
    if (IndirectCall) { NumIndirectCantEncrypt++; }
    return false;
  }

  FuncInfo &CalleeInfo = FunctionInfo[Callee];
  Value *Clone = getCloneCalledValue(CS, CalleeInfo);
  if (!Clone || CalleeInfo.ArgNodes.empty()) {
    if (IndirectCall) { NumIndirectCantEncrypt++; }
    return false;
  }

  // We create a mapping of the formal argument nodes in the callee function and
  // actual argument nodes in the caller function's graph.
  DSGraph::NodeMapTy NodeMap;
  DSGraph *CalleeG = DSA->getDSGraph(*Callee);

  // getArgNodesForCall places the return node and the vanode in the
  // first two slots of the vector, followed by the nodes for the regular
  // pointer arguments.
  std::vector<DSNodeHandle> ArgNodes;
  getArgNodesForCall(CalleeG, DSCS, ArgNodes);

  // First the return value
  DSNodeHandle CalleeRetNode = ArgNodes[0];
  DSGraph::computeNodeMapping(CalleeRetNode, DSCS.getRetVal(), NodeMap);

  // Then VarArgs
  DSNodeHandle CalleeVarArgNode = ArgNodes[1];
  DSGraph::computeNodeMapping(CalleeVarArgNode, DSCS.getVAVal(), NodeMap);

  // And last the regular arguments.
  for (unsigned int i = 0; i < DSCS.getNumPtrArgs() && i + 2 < ArgNodes.size(); i++) {
    DSGraph::computeNodeMapping(ArgNodes[i + 2], DSCS.getPtrArg(i), NodeMap);
  }

  // Collect the arguments and masks to pass to call
  SmallVector<Value*, 8> Args;
  unsigned int i = 0;
  for (unsigned int e = CS.getFunctionType()->getNumParams(); i < e; i++) {
    Args.push_back(CS.getArgOperand(i));
  }

  for (const DSNode *N : CalleeInfo.ArgNodes) {
    Value *Mask = P.getMaskForNode(NodeMap[N]);
    Args.push_back(Mask);
  }

  // VarArgs go after masks
  for (unsigned int e = CS.arg_size(); i < e; i++) {
    Args.push_back(CS.getArgOperand(i));
  }

  // Do replacement
  Instruction *CI = CS.getInstruction();
  Value *Call;
  if (CS.isCall()) {
    Call = CallInst::Create(Clone, Args, "", CI);
  } else {
    InvokeInst *II = cast<InvokeInst>(CI);
    Call = InvokeInst::Create(Clone, II->getNormalDest(), II->getUnwindDest(), Args, "", II);
  }
  CallSite NewCS(Call);
  NewCS.setCallingConv(CS.getCallingConv());

  CI->replaceAllUsesWith(Call);
  P.replace(CI, Call);
  CI->eraseFromParent();

  return true;
}

namespace {
// Traverse all Instructions in a Function and get the node for that
// Instruction. This is to make the mask dump more useful. The scalar map for
// the DSGraph for any functions can include values not in that function. To
// limit the size of the mask dump, we only print values that we have fetched a
// node for, these are added to the MemberMap in ContextSensitivePEA. Even if an
// instruction is a member of an equivalence class, we may not get the node for
// that instruction. This fetches the node for all instructions, but it does not
// get or assign any masks, so it will not change any statistics about the
// number of masks.
void addAllInstructions(const Function *F, PointerEquivalenceAnalysis &P) {
  for (const Instruction &I : instructions(F)) {
    P.getNode(&I);
  }
}
}

bool CSDataRando::runOnModule(Module &M) {
  DSA = &getAnalysis<BUMarkDoNotEncrypt>();
  MaskTy = TypeBuilder<mask_t, false>::get(M.getContext());
  FunctionWrappers &FW = getAnalysis<FunctionWrappers>();

  {
    // Gather statistics on the globals
    DenseMap<const DSNode*, unsigned int> GlobalClassSizes;
    DSGraph *GG = DSA->getGlobalsGraph();
    for (GlobalVariable &GV : M.getGlobalList()) {
      if (!(GV.isDeclaration() || PointerEquivalenceAnalysis::shouldIgnoreGlobal(GV))) {
        GlobalClassSizes[GG->getNodeForValue(&GV).getNode()] += 1;
      }
    }

    NumGlobalECs = GlobalClassSizes.size();
    for (auto i : GlobalClassSizes) {
      if (i.second > MaxSizeGlobalEC) {
        MaxSizeGlobalEC = i.second;
      }
    }
  }

  findGlobalNodes(M);
  findArgNodes(M);

  // Find which functions may need cloning. If we have a DSGraph for the
  // function, consider it a candidate for cloning.
  std::vector<Function *> OriginalFunctions;
  for (Function &F : M) {
    if (!F.isDeclaration() && DSA->hasDSGraph(F)) {
      OriginalFunctions.push_back(&F);
    }
  }

  // Perform cloning of the original functions
  Function *Main = M.getFunction("main");
  for  (Function *Original : OriginalFunctions) {
    // Handle the main function
    if (Main && Original == Main) {
      // Never clone main
      OldToNewFuncMap[Original] = nullptr;
      // If main has no uses then we can encrypt the arguments to main. To allow
      // the arg nodes to be encrypted we clear ArgNodes.
      if (Original->uses().begin() == Original->uses().end()) {
        FunctionInfo[Original].ArgNodes.clear();
      }
      continue;
    }

    // Maybe make a clone, if a clone was not made nullptr is returned.
    OldToNewFuncMap[Original] = makeFunctionClone(Original);
  }

  // File to potentially print diagnostic information
  std::unique_ptr<tool_output_file> Out(nullptr);
  // If we will be printing diagnostic information, open the file
  if (!PointerEquivalenceAnalysis::PrintEquivalenceClassesTo.empty()) {
    std::error_code error;
    Out.reset(new tool_output_file(PointerEquivalenceAnalysis::PrintEquivalenceClassesTo,
                                   error,
                                   sys::fs::F_None));
    if (error) {
      Out.release();
    }
  }

  // Perform randomization
  DataRandomizer DR(M);
  RandomNumberGenerator *RNG = M.createRNG(this);

  FuncInfo empty;
  ContextSensitivePEA GGPEA(*RNG, M.getContext(), empty, *DSA->getGlobalsGraph());
  DR.encryptGlobalVariables(M, GGPEA);

  // All original functions with DSGraphs will be in OldToNewFuncMap. If a clone
  // was not made, then the entry will map to nullptr.
  for (auto i : OldToNewFuncMap) {
    Function *Original = i.first;
    Function *Clone = i.second;
    FuncInfo &FI = FunctionInfo[Original];
    DSGraph *Graph = DSA->getDSGraph(*Original);

    if (Clone) {
      // Perform randomization of the cloned function
      CloneFunctionPEA CP(*RNG, M.getContext(), FI, *Graph, &GGPEA);
      DR.instrumentMemoryOperations(*Clone, CP, NULL);
      DR.wrapLibraryFunctions(*Clone, CP, FW);
      replaceWithClones(Clone, FI, CP, Graph);

      if (Out.get()) {
        // Add all Instructions before dumping to make the dump more complete.
        addAllInstructions(Clone, CP);
        Out->os() << "*** Equivalence classes for: " << Clone->getName() << " ***\n";
        CP.printEquivalenceClasses(Out->os());
        Out->os() << "*** End of equivalence classes for: " << Clone->getName() << " ***\n";
      }
    }

    // Perform randomization of the original function
    FunctionPEA FP(*RNG, M.getContext(), FI, *Graph, &GGPEA, !Clone);
    DR.instrumentMemoryOperations(*Original, FP, NULL);
    DR.wrapLibraryFunctions(*Original, FP, FW);
    replaceWithClones(Original, FI, FP, Graph);

    // Encrypt main args using the main function's PEA
    if (Main && Original == Main) {
      DR.encryptMainArgs(M, FP, FW);
    }

    if (Out.get()) {
      // Add all Instructions before dumping to make the dump more complete.
      addAllInstructions(Original, FP);
      Out->os() << "*** Equivalence classes for: " << Original->getName() << " ***\n";
      FP.printEquivalenceClasses(Out->os());
      Out->os() << "*** End of equivalence classes for: " << Original->getName() << " ***\n";
    }
  }

  // Replace remaining uses of original functions with clones.
  replaceOriginalsWithClones();

  if (Out.get()) {
    Out->os() << "*** Equivalence classes for global variables ***\n";
    GGPEA.printEquivalenceClasses(Out->os());
    Out->os() << "*** End of equivalence classes for global variables ***\n";
    Out->keep();
  }

  return true;
}

bool CSDataRando::doFinalization(Module &M)  {
  NumClasses = NumMasks + NumNotEncrypted;
  return false;
}

bool CSDataRando::replaceOriginalsWithClones() {
  for (auto i : OldToNewFuncMap) {
    Function *Original = i.first;
    Function *Clone = i.second;
    // If there is no clone function there is nothing to replace. If there is an
    // external function in the equivalence class of functions callable from
    // callsites to this function, then we do not want to call the clone of this
    // function in indirect calls.
    if (!Clone || !FunctionInfo[Original].CanReplaceAddress) {
      continue;
    }

    Constant *CloneCast = ConstantExpr::getBitCast(Clone, Original->getType());
    std::vector<User *> toReplace;
    for (auto i = Original->user_begin(), e = Original->user_end(); i != e; i++) {
      CallSite CS(*i);
      if (CS) {
        if (CS.getCalledValue()->stripPointerCasts() == Original) {
          continue;
        }
      }
      toReplace.push_back(*i);
    }

    while (!toReplace.empty()) {
      User *user = toReplace.back();
      toReplace.pop_back();
      if (Constant *C = dyn_cast<Constant>(user)) {
        if (!isa<GlobalValue>(C)) {
          std::vector<Use *> constantToReplace;
          for (Use &u : user->operands()) {
            if (u.get() == Original) {
              constantToReplace.push_back(&u);
            }
          }
          for (Use *u : constantToReplace) {
            // If there are multiple uses, the first call to handleOperandChange
            // can replace both of them.
            if (u->get() == Original) {
              C->handleOperandChange(Original, CloneCast, u);
            }
          }
        }
      } else {
        user->replaceUsesOfWith(Original, CloneCast);
      }
    }
  }
  return true;
}

void CSDataRando::findArgNodes(Module &M) {
  // Create function equivalence classes from the global equivalence classes.
  EquivalenceClasses<const GlobalValue*> &GlobalECs = DSA->getGlobalECs();
  EquivalenceClasses<const Function*> FunctionECs;

  for (auto ei = GlobalECs.begin(), ee = GlobalECs.end(); ei != ee; ei++) {
    // Ignore non-leader values.
    if (!ei->isLeader()) { continue; }

    const Function *Leader = nullptr;
    for (auto mi = GlobalECs.member_begin(ei), me = GlobalECs.member_end();
         mi != me;
         mi++) {
      // Only look at functions.
      if (const Function *F = dyn_cast<Function>(*mi)) {
        if (Leader) {
          FunctionECs.unionSets(Leader, F);
        } else {
          Leader = FunctionECs.getOrInsertLeaderValue(F);
        }
      }
    }
  }

  // Make sure all Functions are part of an equivalence class. This is important
  // since non-address taken functions may not appear in GlobalECs.
  for (Function &F : M) {
    if (!F.isDeclaration()) {
      FunctionECs.insert(&F);
    }
  }

  // Go through all equivalence classes and determine the additional
  // arguments.
  for (auto ei = FunctionECs.begin(), ee = FunctionECs.end();ei != ee; ei++) {
    if (ei->isLeader()) {
      NumFunctionECs++;
      std::vector<const Function*> Functions;
      Functions.insert(Functions.end(), FunctionECs.member_begin(ei), FunctionECs.member_end());

      // If we can't safely replace uses of the function's address with its
      // clone's address then we can't safely transform indirect calls to this
      // equivalence class. We still find the arg nodes for each function to
      // replace direct calls to these functions.
      if (!DSA->canReplaceAddress(ei->getData())) {
        NumFunECsWithExternal++;
        for (const Function *F : Functions) {
          if (!F->isDeclaration()) {
            findFunctionArgNodes(F);
            FunctionInfo[F].CanReplaceAddress = false;
          }
        }
      } else {
        findFunctionArgNodes(Functions);
      }
    }
  }
}

// Find the number of arguments we need to add to the functions.
void CSDataRando::findFunctionArgNodes(const std::vector<const Function *> &Functions) {
  std::vector<DSNodeHandle> RootNodes;
  for (const Function *F : Functions) {
    DSGraph *G = DSA->getDSGraph(*F);
    G->getFunctionArgumentsForCall(F, RootNodes);
  }

  // No additional args to pass.
  if (RootNodes.size() == 0) {
    return;
  }

  DenseSet<const DSNode*> MarkedNodes;
  for (DSNodeHandle &NH : RootNodes) {
    if (DSNode *N = NH.getNode()) {
      N->markReachableNodes(MarkedNodes);
    }
  }

  // Remove global nodes from the arg nodes. If we are using the bottom-up
  // analysis then if a node is a global node all contexts will use the global map.
  for (auto i : GlobalNodes) {
    MarkedNodes.erase(i);
  }

  // Remove any nodes that are marked do not encrypt.
  SmallVector<const DSNode*, 8> MarkedNodeWorkList;
  for (auto i : MarkedNodes) {
    if (i->isDoNotEncryptNode()) {
      MarkedNodeWorkList.push_back(i);
    }
  }
  for (auto i : MarkedNodeWorkList) {
    MarkedNodes.erase(i);
  }

  if (MarkedNodes.empty()) {
    return;
  }

  // Create a FuncInfo entry for each of the functions with the arg nodes that
  // need to be passed
  for (const Function *F : Functions) {
    FuncInfo &FI = FunctionInfo[F];
    FI.ArgNodes.insert(FI.ArgNodes.end(), MarkedNodes.begin(), MarkedNodes.end());
  }
}

// Maybe make a clone, if a clone is made, return a pointer to it, if a clone
// was not made return nullptr.
Function *CSDataRando::makeFunctionClone(Function *F) {
  // Now we know how many arguments need to be passed, so we make the clones
  FuncInfo &FI = FunctionInfo[F];
  if (FI.ArgNodes.size() == 0) {
    // No additional args to pass, no need to clone.
    return nullptr;
  }
  // Determine the type of the new function, we insert the new parameters for
  // the masks after the normal arguments, but before any va_args
  Type *MaskTy = TypeBuilder<mask_t, false>::get(F->getContext());
  FunctionType *OldFuncTy = F->getFunctionType();
  std::vector<Type*> ArgTys;
  ArgTys.insert(ArgTys.end(), OldFuncTy->param_begin(), OldFuncTy->param_end());
  ArgTys.insert(ArgTys.end(), FI.ArgNodes.size(), MaskTy);
  FunctionType *CloneFuncTy = FunctionType::get(OldFuncTy->getReturnType(), ArgTys, OldFuncTy->isVarArg());

  Function *Clone = Function::Create(CloneFuncTy, Function::InternalLinkage, F->getName() + "_CONTEXT_SENSITIVE");
  F->getParent()->getFunctionList().insert(F->getIterator(), Clone);

  Function::arg_iterator CI = Clone->arg_begin(), CE = Clone->arg_end();

  // Map the old arguments to the clone arguments and set the name of the
  // clone arguments the same as the original.
  for (Function::arg_iterator i = F->arg_begin(), e = F->arg_end(); i != e && CI != CE; i++, CI++) {
    FI.OldToNewMap[&*i] = &*CI;
    CI->setName(i->getName());
  }

  // Set the name of the arg masks and associate them with the nodes they are
  // the masks for.
  for (unsigned i = 0, e = FI.ArgNodes.size(); i != e; ++i, ++CI) {
    CI->setName("arg_mask");
    FI.ArgMaskMap[FI.ArgNodes[i]] = &*CI;
  }

  SmallVector<ReturnInst*, 8> Returns;
  CloneFunctionInto(Clone, F, FI.OldToNewMap, false, Returns);
  Clone->setCallingConv(F->getCallingConv());

  // Invert OldToNewMap
  for (auto I : FI.OldToNewMap) {
    FI.NewToOldMap[I.second] = I.first;
  }

  NumClones++;
  return Clone;
}


void CSDataRando::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BUMarkDoNotEncrypt>();
  AU.addRequired<FunctionWrappers>();
}

char CSDataRando::ID = 0;
static RegisterPass<CSDataRando> X("cs-data-rando", "Context sensitive data randomization pass");
