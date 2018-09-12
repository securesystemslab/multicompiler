//===- MarkDoNotEncrypt.h - Mark which nodes not to encrypt ---------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/DataRando/MarkDoNotEncrypt.h"
#include "llvm/DataRando/DataRando.h"
#include "llvm/IR/InstVisitor.h"
#include "dsa/DSGraph.h"
#include "dsa/DSNode.h"
#include "dsa/DSSupport.h"

using namespace llvm;

namespace {
void markDoNotEncryptForAllReachable(DSNode *Root, DSNodeHandle &NH, DenseSet<const DSNode*> &Visited) {
  DSNode *N = NH.getNode();
  if (!N || Visited.count(N)) {
    return;
  }

  N->setDoNotEncryptMarker();
  Visited.insert(N);
  N->mergeReasons(*Root);

  for (auto i = N->edge_begin(), e = N->edge_end(); i != e; i++) {
    markDoNotEncryptForAllReachable(Root, i->second, Visited);
  }
}

// Starting from a DoNotEncryptReachableNode, add the DoNotEncrypt marking for
// all nodes reachable from that root node and propagate all reasons.
void markDoNotEncryptForAllReachable(DSNode *Root) {
  assert(Root->isDoNotEncryptReachableNode() &&
         "Starting recursive DoNotEncrypt marking from a node that is not a DoNotEncryptReachableNode");
  DenseSet<const DSNode*> Visited;
  DSNodeHandle NH(Root);
  markDoNotEncryptForAllReachable(Root, NH, Visited);
}

struct MarkDoNotEncryptVisitor : public InstVisitor<MarkDoNotEncryptVisitor> {
  MarkDoNotEncryptVisitor(DataStructures &DSA)
      : DSA(DSA) {
  }

  std::vector<CallSite> CallSites;

  void visitCallSite(CallSite CS) {
    CallSites.push_back(CS);
  }

  void visitVAStartInst (VAStartInst &V) {
    markVAList(V.getArgOperand(0), DSA.getOrCreateGraph(V.getParent()->getParent()));
  }

  void visitVACopyInst(VACopyInst &V) {
    DSGraph *G = DSA.getOrCreateGraph(V.getParent()->getParent());
    markVAList(V.getArgOperand(0), G);
    markVAList(V.getArgOperand(1), G);
  }

  void markVAList(Value *V, DSGraph *G) {
    if (!G->hasNodeForValue(V)) {
      return;
    }
    DSNode* N = G->getNodeForValue(V).getNode();
    N->setDoNotEncryptMarker("Contains va_list");
    for (auto i = N->edge_begin(), e = N->edge_end(); i != e; i++) {
      i->second.getNode()->setDoNotEncryptMarker("Contains va_list");
    }
  }

  DataStructures &DSA;
};

// Traverse all DSNodes in a DSGraph, for any node that has the
// DoNotEncryptReachable marker add the DoNotEncrypt marker to all reachable
// nodes.
void propogateDoNotEncryptForDSGraph(DSGraph *G) {
  for (auto I = G->node_begin(), E = G->node_end(); I != E; ++I) {
    if (I->isDoNotEncryptReachableNode()) {
      markDoNotEncryptForAllReachable(&*I);
    }
  }
}
}

void BUMarkDoNotEncrypt::propogateDoNotEncrypt(Module &M) {
  for (const Function &F : M) {
    if (hasDSGraph(F)) {
      propogateDoNotEncryptForDSGraph(getDSGraph(F));
    }
  }
  propogateDoNotEncryptForDSGraph(getGlobalsGraph());
}

// The analysis performed by this pass is very similar to the analysis done by
// SteensgaardsPEA to determine what can't be encrypted. This analysis is meant
// to be used by the context sensitive data randomization so the way things are
// marked to not be encrypted is a bit different.
//
// TODO: Unify the way SteensgaardsPEA and this pass do the analysis.
bool BUMarkDoNotEncrypt::runOnModule(Module &M) {
  init(&getAnalysis<EquivBUDataStructures>(), true, true, false, true);
  FunctionWrappers &FW = getAnalysis<FunctionWrappers>();

  // Visit the program and mark nodes for problematic values.
  MarkDoNotEncryptVisitor V(*this);
  V.visit(M);

  findDoNotReplaceFunctions(V.CallSites);

  for (CallSite CS : V.CallSites) {
    if (!canEncryptCall(CS)) {
      markCallDoNotEncrypt(CS);
    }
    specialCases(CS);
  }

  // Find external globals and mark them.
  DSGraph *GG = getGlobalsGraph();
  for (GlobalVariable &GV : M.getGlobalList()) {
    if (GV.isDeclaration()) {
      if (FW.isRTTIVtable(GV)) {
        // Don't mark do not encrypt for RTTI vtables.
        continue;
      }
      std::string Reason("External global variable: ");
      Reason.append(GV.getName().str());
      GG->getNodeForValue(&GV).getNode()->setDoNotEncryptReachableMarker(Reason);
    }
  }

  // Make sure we have a DSGraph for all functions and then find any values we
  // can't encrypt in the scalar map.

  // Create a set of all the DSGraphs
  DenseSet<DSGraph*> Graphs;
  // Insert the globals graph, we are going to examine this too
  Graphs.insert(GG);

  for (const Function &F : M) {
    if (!F.isDeclaration()) {
      Graphs.insert(getOrCreateGraph(&F));
    }
  }

  // Examine the scalar maps of all graphs and mark anything with a type we
  // can't encrypt.
  for (DSGraph *G : Graphs) {
    DSScalarMap &SM = G->getScalarMap();
    for (auto i : SM) {
      if (!FW.typeCanBeEncrypted(i.first->getType())) {
        std::string Msg("Unencryptable data type: ");
        raw_string_ostream ostream(Msg);
        i.first->getType()->print(ostream);
        i.second.getNode()->setDoNotEncryptReachableMarker(ostream.str());
      }
    }
  }

  // Handle the case where main may have a third environ argument.
  Function *Main = M.getFunction("main");
  if (Main && Main->arg_size() == 3) {
    auto A = Main->arg_begin();
    A++;
    A++;
    DSGraph *MainG = getOrCreateGraph(Main);
    if (MainG->hasNodeForValue(&*A)) {
      MainG->getNodeForValue(&*A).getNode()->setDoNotEncryptReachableMarker("Environment argument to main");
    }
  }

  // Perform bottom up propagation of the do not encrypt flag by doing another
  // bottom up inlining of DSGraphs.
  auto r = runOnModuleInternal(M);
  restoreCorrectCallGraph();

  // Propogate DoNotEncrypt marker.
  propogateDoNotEncrypt(M);

  return r;
}

void BUMarkDoNotEncrypt::specialCases(CallSite CS) {
  Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
  if (F == nullptr) {
    return;
  }
  auto Name = F->getName();
  if (Name == "gmtime_r" || Name == "localtime_r") {
    if (CS.arg_size() < 2) {
      return;
    }
    DSGraph *G = getOrCreateGraph(CS->getFunction());
    Value *Arg = CS.getArgOperand(1);
    if (!G->hasNodeForValue(Arg)) {
      return;
    }
    const DSNode *N = G->getNodeForValue(Arg).getNode();
    for (auto i = N->edge_begin(), e = N->edge_end();
         i != e; i++) {
      i->second.getNode()->setDoNotEncryptMarker("Static string returned by reentrant function");
    }
  }
}

void BUMarkDoNotEncrypt::findDoNotReplaceFunctions(std::vector<CallSite> &Calls) {
  const DSScalarMap &ScalarMap = getGlobalsGraph()->getScalarMap();
  const DSCallGraph &CallGraph = getCallGraph();
  size_t count;
  size_t passcount = 0;
  // Since this identifies more functions that we don't encrypt indirect calls
  // to, and this information is used by canEncryptCall, so it is necessary to
  // loop through the calls multiple times to find a fixed point.
  do {
    count = DoNotReplaceAddress.size();
    for (CallSite CS : Calls) {
      if (canEncryptCall(CS)) {
        continue;
      }

      // If we can't encrypt a call, this could be because it may target an
      // external function, so we don't want to pass it any function pointers to
      // cloned functions
      DSGraph *G = getOrCreateGraph(CS->getParent()->getParent());
      svset<const Function*> Functions;
      for (auto &A : CS.args()) {
        if (G->hasNodeForValue(A)) {
          G->getNodeForValue(A).getNode()->addFullFunctionSet(Functions);
        }
      }

      // If it is an indirect call, also add the potential callees
      if (!isa<Function>(CS.getCalledValue()->stripPointerCasts())) {
        auto mi = CallGraph.callee_begin(CS);
        if (mi != CallGraph.callee_end(CS)) {
          // Insert just a single callee since all callees will have the same
          // node in the globals graph.
          Functions.insert(*mi);
        }
      }

      for (const Function *F : Functions) {
        // Use the GlobalEC leader to represent each possible function.
        DoNotReplaceAddress.insert(ScalarMap.getLeaderForGlobal(F));
      }
    }

    // Break out of loop if we do too many passes.
    if (++passcount > 100000) {
      errs() << "warning: fixed point of escaping function addresses not reached\n";
      break;
    }
  } while (count != DoNotReplaceAddress.size());
}

bool BUMarkDoNotEncrypt::canEncryptCall(CallSite CS) {
  FunctionWrappers &FW = getAnalysis<FunctionWrappers>();
  Value *Callee = CS.getCalledValue()->stripPointerCasts();
  Function *F = dyn_cast<Function>(Callee);
  if (F) {
    // If we have a definition for this function or we have a wrapper for this
    // function or if it is a memory management function or intrinsic, then we
    // don't consider it external and don't need to mark anything.
    if (!F->isDeclaration() || F->isIntrinsic() || FW.hasWrapperFunction(F)
        || FW.isMemManagementFunction(F) || FW.isJmpFunction(F)
        || F->getName() == "__crosscheck"
        || F->getName() == "__cxa_atexit") {
      return true;
    }

    // Else it is an external function without a wrapper and we cannot encrypt
    // anything passed to or returned from it.
    return false;
  } else {
    if (CS.isInlineAsm()) {
      // We cannot encrypt anything passed to inline asm
      return false;
    } else {
      // Handle indirect calls.

      // We handle indirect calls conservatively. If the list of functions
      // called at this call site contains external functions, we force all
      // arguments to be unencrypted, even if we have wrappers for all the
      // functions.

      // TODO: Handle the case where we have wrappers for all functions callable
      //       from this call site. This will allow more things to be encrypted.

      // Process the list of possible functions that can be called from this
      // call site. If it only contains internal functions, the pointer
      // analysis can correctly handle the call site, so we can encrypt
      // anything passed to the function. If it contains external functions,
      // we force all arguments to be unencrypted.
      const DSGraph *G = getOrCreateGraph(CS->getParent()->getParent());
      const DSNode *CalleeNode = G->getNodeForValue(Callee).getNode();
      if (CalleeNode->isCompleteNode() && CalleeNode->isExternFuncNode()) {
        return false;
      }

      const DSCallGraph &CG = getCallGraph();
      auto ci = CG.callee_begin(CS), ce = CG.callee_end(CS);
      // If we have no callees in the call graph, we cannot encrypt this
      // callsite since this can occur if the callsite may target external
      // functions. Additionally, without any possible callees we cannot create
      // a mapping from the caller graph to the callee graph.
      if (ci == ce) {
        return false;
      }
      for (; ci != ce; ci++) {
        // If we can't replace constant addresses of a callee function with its
        // clones or it can target an external function, then we cannot encrypt
        // this callsite.
        if ((!canReplaceAddress(*ci)) || (*ci)->isDeclaration()) {
          return false;
        }
      }

      // We have gone through all possible cases and not found a reason not to
      // encrypt.
      return true;
    }
  }
}

bool BUMarkDoNotEncrypt::canReplaceAddress(const Function *F) {
  const DSGraph *GG = getGlobalsGraph();
  const GlobalValue *Leader = GG->getScalarMap().getLeaderForGlobal(F);
  if (GG->hasNodeForValue(Leader) && GG->getNodeForValue(Leader).getNode()->isExternFuncNode()) {
    return false;
  }
  return !DoNotReplaceAddress.count(Leader);
}

void BUMarkDoNotEncrypt::markCallDoNotEncrypt(CallSite CS) {
  DSGraph *G = getOrCreateGraph(CS->getParent()->getParent());

  // Create a collection containing the call instruction and all arguments.
  SmallVector<const Value*, 8> Values;
  Values.push_back(CS.getInstruction());
  Values.append(CS.arg_begin(), CS.arg_end());

  // Traverse the collection and mark all nodes DoNotEncrypt.
  for (const Value *V : Values) {
    if (G->hasNodeForValue(V)) {
      DSNodeHandle &NH = G->getNodeForValue(V);
      std::string Reason;
      if (const Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
        Reason.append("Unwrapped call to: ");
        Reason.append(F->getName().str());
      } else {
        Reason.append("Unencryptable indirect call");
      }
      NH.getNode()->setDoNotEncryptReachableMarker(Reason);
    }
  }
}


void BUMarkDoNotEncrypt::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<EquivBUDataStructures>();
  AU.addRequired<FunctionWrappers>();
  AU.setPreservesAll();
}

char BUMarkDoNotEncrypt::ID = 0;
static RegisterPass<BUMarkDoNotEncrypt> X("bu-mark-do-not-encrypt",
                                          "Mark which nodes not to encrypt and perform bottom up propagation.");
