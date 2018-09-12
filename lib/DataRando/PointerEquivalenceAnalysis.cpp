//===- PointerEquivalenceAnalysis.cpp - Build equivalence classes ---------===//

#define DEBUG_TYPE "DataRando"

#include "llvm/Support/FileSystem.h"
#include "llvm/DataRando/PointerEquivalenceAnalysis.h"
#include "llvm/DataRando/DataRando.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "dsa/DSGraph.h"
#include "dsa/DSGraphTraits.h"

using namespace llvm;

char SteensgaardsPEA::ID = 0;
static RegisterPass<llvm::SteensgaardsPEA> X("pointer-equivalence", "Group pointers into equivalence classes");

STATISTIC(NumNodes, "Number of DSNodes");
STATISTIC(NumHeapNodes, "Number of DSNodes with Heap flag");
STATISTIC(NumHeapClasses, "Number of equivalence classes on the heap");
STATISTIC(NumEquivalenceClasses, "Number of equivalence classes assigned masks");
STATISTIC(NumEquivalenceClassesAccessed, "Number of equivalence classes accessed by instructions");
STATISTIC(NumCantEncryptGlobal, "Number of equivalence classes we can't encrypt because they contain external globals");
STATISTIC(NumCantEncryptType, "Number of equivalence classes we can't encrypt because they contain unencryptable types");
STATISTIC(NumCantEncryptExternalCall, "Number of equivalence classes we can't encrypt because they escape to unwrapped external functions");
STATISTIC(NumCantEncrypteVAList, "Number of equivalence classes we can't encrypt because they contain VA_List values");
STATISTIC(NumCantEncryptClasses, "Number of equivalence classes that cannot be encrypted");
STATISTIC(NumSafeClasses, "Number of equivalence classes composed entirely of safe accesses");
STATISTIC(NumIncompleteNodes, "Number of incomplete equivalence classes");
STATISTIC(NumEffectiveEquivalenceClasses, "Effective number of equivalence classes");
STATISTIC(NumMasks, "Number of random masks assigned to equivalence classes");
STATISTIC(NumGlobalECs, "Number of equivalence classes containing global variables");
STATISTIC(MaxSizeGlobalEC, "Maximum number of globals contained in a single equivalence class");
STATISTIC(NumNotEncrypted, "Number of equivalence classes which are not encrypted");

cl::opt<unsigned int> PointerEquivalenceAnalysis::EffectiveMaskSize("data-rando-effective-mask-size", cl::init(8));
cl::opt<std::string> PointerEquivalenceAnalysis::PrintEquivalenceClassesTo("print-eq-classes-to", cl::desc("Output the equivalence classes to the specified filename"));
cl::opt<bool> PointerEquivalenceAnalysis::PrintAllocationCounts("print-allocation-counts", cl::desc("Print the number of allocation sites for each equivalence class"), cl::init(false));
static cl::opt<bool> SafetyAnalysis("safety-analysis", cl::desc("Perform safety analysis before assigning xor masks"), cl::init(true));
static cl::opt<std::string> PrintUsageCountsTo("print-eq-class-usage-counts", cl::desc("Output the usage counts of each equivalence class to the specified file"));

void PointerEquivalenceAnalysis::init(RandomNumberGenerator &R, LLVMContext &C) {
  // Effective mask size needs to be a power of 2 equal to or less than
  // sizeof(mask_t). If it is not, set it to be sizeof(mask_t);
  if ((EffectiveMaskSize & (EffectiveMaskSize - 1)) ||
      EffectiveMaskSize > sizeof(mask_t)) {
    EffectiveMaskSize = sizeof(mask_t);
  }
  RNG = &R;
  MaskTy = TypeBuilder<mask_t, false>::get(C);
}

SteensgaardsPEA::SteensgaardsPEA() : ModulePass(ID) {}

void SteensgaardsPEA::warnUnknown(const DSNode *Node) {
  if (UsedUnknownNodes.insert(Node).second) {
    errs() << "Warning: Using node with unknown flag set, Node"
           << static_cast<const void*>(Node) << "\n";
  }
}

void SteensgaardsPEA::assignMask(const NodeHandle &N, Constant *M, StringRef Reason) {
  const DSNode *ND = N.getNode();
  assert(ND && "Assigning mask to null node");
  assert(M && "Mask cannot be null pointer");
  if (ND->isHeapNode() && (!MaskMap.count(ND))) {
    NumHeapClasses++;
  }
  MaskMap[ND] = M;
  if (!Reason.empty()) {
    MaskReason[ND].insert(Reason);
  }
}

namespace {

struct PEAVisitor : public InstVisitor<PEAVisitor> {
  PEAVisitor(const FunctionWrappers &FW, const DSGraph *G)
      : FW(FW), DSG(G) {
  }

  void visitCallSite(CallSite CS) {
    Value *Callee = CS.getCalledValue()->stripPointerCasts();
    Function *F = dyn_cast<Function>(Callee);
    if (F && (F->getName() == "gmtime_r" || F->getName() == "localtime_r")) {
      SpecialCaseCalls.push_back(CS);
    }
    if (F && F->isDeclaration()) {

      // If we have a wrapper for this function or if it is a memory management
      // function or intrinsic, do nothing
      if (F->isIntrinsic() || FW.hasWrapperFunction(F)
          || FW.isMemManagementFunction(F) || FW.isJmpFunction(F)
          || F->getName() == "__crosscheck"
          || F->getName() == "__cxa_atexit") {
        return;
      }

      // Add to ExternalCalls
      ExternalCalls.push_back(CS);
    } else if (!F) {
      // Handle indirect calls.

      // We handle indirect calls conservatively. If the list of functions
      // called at this call site contains external functions, we force all
      // arguments to be unencrypted, even if we have wrappers for all the
      // functions.

      // TODO: Handle the case where we have wrappers for all functions callable
      //       from this call site. This will allow more things to be encrypted.


      if (CS.isInlineAsm()) {
        // We cannot encrypt anything passed to inline asm
        ExternalCalls.push_back(CS);
      } else {
        // Process the list of possible functions that can be called from this
        // call site. If it only contains internal functions, the pointer
        // analysis can correctly handle the call site, so we can encrypt
        // anything passed to the function. If it contains external functions,
        // we can force all arguments to be unencrypted. If there is a mix of
        // internal and external functions, the pointer analysis will be aware
        // of the internal calls, and when we force the arguments to this call
        // site to be unencrypted, this will include their use in the internal
        // calls.
        std::vector<const Function*> CallTargets;
        DSG->getNodeForValue(Callee).getNode()->addFullFunctionList(CallTargets);
        bool HasExternal = false;
        for (const Function *F : CallTargets) {
          if (F->isDeclaration()) {
            HasExternal = true;
          }
        }

        if (HasExternal) {
          ExternalCalls.push_back(CS);
        }
      }
    }
  }

  void visitVAStartInst (VAStartInst &V) {
    VA_Lists.insert(V.getArgOperand(0));
  }

  void visitVACopyInst(VACopyInst &V) {
    VA_Lists.insert(V.getArgOperand(0));
    VA_Lists.insert(V.getArgOperand(1));
  }

  const FunctionWrappers &FW;
  DenseSet<StringRef> MemManagement;
  std::vector<ImmutableCallSite> ExternalCalls;
  DenseSet<Value*> VA_Lists;
  std::vector<CallSite> SpecialCaseCalls;
  const DSGraph *DSG;
};

}

void *SteensgaardsPEA::getAdjustedAnalysisPointer(const void *ID) {
  if (ID == &SteensgaardsPEA::ID) {
    return (SteensgaardsPEA*)this;
  }
  return this;
}

void SteensgaardsPEA::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<SteensgaardDataStructures>();
  AU.addPreserved<SteensgaardDataStructures>();
  AU.addRequiredTransitive<FunctionWrappers>();
  AU.addPreserved<FunctionWrappers>();
}


bool SteensgaardsPEA::runOnModule(Module &M) {
  init(*M.createRNG(this), M.getContext());
  const SteensgaardDataStructures *DSA = &getAnalysis<SteensgaardDataStructures>();
  DL = &M.getDataLayout();
  CTX = &M.getContext();
  FW = &getAnalysis<FunctionWrappers>();

  // Combine Value->Node mapping from the global graph and the result graph

  // First do the global graph
  addMappingsFromGraph(DSA->getGlobalsGraph());

  // Then do the result graph so that mappings in the result graph will
  // overwrite mappings from the global graph. The result graph will capture
  // alias relationship with values in the program and global values, while the
  // globals graph will only contain relationships between globals.
  addMappingsFromGraph(DSA->getResultGraph());

  // Find external globals, and assign them the mask 0
  {
    unsigned int Initial = MaskMap.size();
    DenseMap<const DSNode*, unsigned int> GlobalClassSizes;
    for (GlobalVariable &GV : M.getGlobalList()) {
      if (!shouldIgnoreGlobal(GV)) {
        if (GV.isDeclaration()) {
          if (FW->isRTTIVtable(GV)) {
            // Ignore vtables used for RTTI. Don't assign null mask, and don't
            // count them when counting the number of globals in each class.
            continue;
          }
          std::string Msg("External global variable: ");
          Msg.append(GV.getName().str());
          assignMaskRecursively(getNode(&GV), nullMask(), Msg);
        } else {
          GlobalClassSizes[getNode(&GV).getNode()] += 1;
        }
      }
    }
    NumGlobalECs = GlobalClassSizes.size();
    for (auto i : GlobalClassSizes) {
      if (i.second > MaxSizeGlobalEC) {
        MaxSizeGlobalEC = i.second;
      }
    }
    NumCantEncryptGlobal = MaskMap.size() - Initial;
  }

  // Traverse all nodes
  DenseSet<const DSNode*> Nodes;
  DenseSet<const DSNode*> IncompleteNodes;
  {
    unsigned int Initial = MaskMap.size();
    for (auto i : NodeMap) {
      Nodes.insert(i.second.Node);

      if (!i.second.Node->isCompleteNode()) {
        IncompleteNodes.insert(i.second.Node);
      }

      // Find anything that has a type we cannot encrypt and recursively assign
      // the mask 0
      if (! FW->typeCanBeEncrypted(i.first->getType())) {
        std::string Msg("Unencryptable data type: ");
        raw_string_ostream ostream(Msg);
        i.first->getType()->print(ostream);
        assignMaskRecursively(i.second, nullMask(), ostream.str());
      }
    }

    // We can compute this statistic because the only masks that will be
    // assigned in the previous loop are from FILE* values
    NumCantEncryptType = MaskMap.size() - Initial;
  }

  // We have collected all of the nodes, use this information to compute the
  // NumNodes statistic, since it is readily available.
  NumNodes = Nodes.size();
  NumIncompleteNodes = IncompleteNodes.size();

  for (const DSNode *n : Nodes) {
    if (n->isHeapNode()) {
      NumHeapNodes++;
    }
  }

  // Traverse the program
  PEAVisitor E(getAnalysis<FunctionWrappers>(), DSA->getResultGraph());
  E.visit(M);

  {
    unsigned int Initial = MaskMap.size();
    handleUnwrappedExternalCalls(E.ExternalCalls);
    NumCantEncryptExternalCall = MaskMap.size() - Initial;
  }

  // Find all va_lists and assign the list and its immediate children the mask
  // 0. The list and the memory it points to are created by the llvm.va_start
  // intrinsic, so it is not encrypted, but when the args are accessed,
  // llvm.va_arg is lowered by the front end. The analysis creates the correct
  // nodes for this. Since we just white-list the va_list struct and the memory
  // it points to, which contains the actual arguments, any memory referenced by
  // pointer arguments can still be encrypted.
  //
  // TODO: Another option would be to wrap the intrinsic and encrypt the va_list
  //       and the memory it points to, evaluate both options and choose which
  //       is the best.
  {
    unsigned int Initial = MaskMap.size();
    for (Value *V : E.VA_Lists) {
      const DSNode *N = getNode(V).Node;
      assignMask(N, nullMask(), "va_list");
      for (const DSNode *C : *N) {
        assignMask(C, nullMask(), "va_list");
      }
    }
    NumCantEncrypteVAList = MaskMap.size() - Initial;
  }

  {
    // Handle special cases. For now this is only calls to gmtime_r or localtime_r.
    for (CallSite CS : E.SpecialCaseCalls) {
      Function *F = cast<Function>(CS.getCalledValue()->stripPointerCasts());
      if (F->getName() == "gmtime_r" || F->getName() == "localtime_r") {
        if (CS.arg_size() < 2) {
          continue;
        }
        Value *Arg = CS.getArgOperand(1);
        const DSNode *N = getNode(Arg).Node;
        for (auto i = N->edge_begin(), e = N->edge_end();
             i != e; i++) {
          assignMask(i->second.getNode(), nullMask(), "Static string returned by reentrant function");
        }
      } else {
        llvm_unreachable("The only special cases are calls to gmtime_r or localtime_r.");
      }
    }
  }

  // All classes that can't be encrypted have now been identified, so compute that statistic
  NumCantEncryptClasses = MaskMap.size();

  // Handle the case where main may have a third environ argument.
  Function *Main = M.getFunction("main");
  if (Main && Main->arg_size() == 3) {
    auto A = Main->arg_begin();
    A++;
    A++;
    assignMaskRecursively(getNode(&*A), nullMask(), "Environment argument to main");
  }

  // Now identify safe equivalence classes
  if (SafetyAnalysis) {
    safetyAnalysis();
  }

  // After safety analysis has been performed all equivalence classes that will
  // not be encrypted have been found.
  NumNotEncrypted = MaskMap.size();
  return false;
}

bool SteensgaardsPEA::doFinalization(Module &) {
  // If there are classes that are not encrypted then add 1 to the number of
  // classes. All not encrypted classes are effectively members of a single
  // equivalence class with the mask 0.
  NumEffectiveEquivalenceClasses = NumEquivalenceClasses - NumNotEncrypted + (NumNotEncrypted ? 1 : 0);

  NumEquivalenceClassesAccessed = AccessCounts.size();

  if (!PrintUsageCountsTo.empty()) {
    std::error_code err;
    raw_fd_ostream S(PrintUsageCountsTo, err, sys::fs::F_None);
    if (!err) {
      S << "Node,Access count\n";
      for (auto i : AccessCounts) {
        S << static_cast<const void*>(i.first) << ',' << i.second << ',' << (i.first->getNodeFlags() & DSNode::Composition) << '\n';
      }
    }
  }
  return false;
}

void PointerEquivalenceAnalysis::printClass(raw_ostream &S, const DSNode* N, Value *Mask, const StringSet<> &Reasons, std::vector<const Value*> &ValueList) {
  if (Mask) {
    S << "Mask = " << *Mask << '\n';
  }

  if (!Reasons.empty()) {
    S << "Mask reason:\n";
    for (auto &str : Reasons) {
      S << '\t' << str.first() << '\n';
    }
  }

  S << "DSNode:\n";
  N->print(S, N->getParentGraph());

  S << "Allocation count: " << N->numAllocations() << '\n';

  S << "Class members:\n";
  for (auto V : ValueList) {
    if (const Function* F = dyn_cast<Function>(V)) {
      S << "Function: " << Function::getRealLinkageName(F->getName()) << '\n';
    } else {
      S << *V << '\n';
    }
  }
  S << "********************************************************************************\n";
}

bool SteensgaardsPEA::printEquivalenceClasses(StringRef Name, const Module &Mdl) {
  DenseMap<const DSNode*, std::vector<const Value*> > ValueLists;
  for (auto I : NodeMap) {
    ValueLists[I.second.Node].push_back(I.first);
  }

  std::error_code error;
  tool_output_file File(Name, error, sys::fs::F_None);
  if (!error) {
    for (auto I : ValueLists) {
      printClass(File.os(), I.first, MaskMap[I.first], MaskReason[I.first], I.second);
    }
  }
  File.keep();
  return false;
}

bool SteensgaardsPEA::printAllocationCounts() {
  for (auto i : MaskMap) {
    if (i.second && AccessCounts[i.first] != 0) {
      errs() << "Node" << i.first << ',' << i.first->numAllocations() << ',' << *i.second << '\n';
    }
  }
  return false;
}

static bool isSafeAddress(const Value *V) {
  // We will consider an address safe if it is a constant and does not escape to
  // external functions.
  if(isa<Constant>(V)) {
    // Examine all users to ensure it does not escape
    for (auto I : V->users()) {
      ImmutableCallSite CS(I);
      if (CS) {
        if (const Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
          if (F->isDeclaration()) {
            // escapes to external function
            return false;
          }
        } else {
          // function is not known
          return false;
        }
      }
    }
    // we have gone through all users and everything seems OK
    return true;
  } else {
    return false;
  }
}

void SteensgaardsPEA::safetyAnalysis() {
  DenseSet<const DSNode*> Nodes;
  DenseSet<const DSNode*> UnsafeNodes;

  // Collect all nodes and find any nodes accessed in an unsafe way in the
  // analysis.
  //
  // TODO: We traverse all the nodes both here and in runOnModule, this
  //       redundancy can be removed if we structure the code a bit more
  //       cleverly
  for (auto I : NodeMap) {
    Nodes.insert(I.second.Node);

    // If this value is potentially an unsafe access, add the node for that
    // value to the collection of unsafe nodes.
    if (!isSafeAddress(I.first)) {
      UnsafeNodes.insert(I.second.Node);
    }
  }

  // Remove unsafe nodes from the collection of nodes
  for (auto I : UnsafeNodes) {
    Nodes.erase(I);
  }

  // What is left are the safe nodes, assign these the mask 0.
  for (auto I : Nodes) {
    assignMask(I, nullMask(), "Safe equivalence class");
  }

  NumSafeClasses = Nodes.size();
}

Value *SteensgaardsPEA::getMaskForNode(const NodeHandle &N) {
  if (!N.Node) {
    return nextMask();
  }

  DEBUG(if (N.Node->isUnknownNode()) { warnUnknown(N.Node); });

  AccessCounts[N.Node] += 1;

  auto I = MaskMap.find(N.Node);
  if (I == MaskMap.end()) {
    Constant *M = nextMask();
    assignMask(N, M);
    // update statistics
    NumMasks++;
    NumEquivalenceClasses = MaskMap.size();
    return M;
  }
  return I->second;
}

void SteensgaardsPEA::addMappingsFromGraph(const DSGraph* DSG) {
  // First add the mappings in the scalar map
  for (const auto &I : DSG->getScalarMap()) {
    NodeMap[I.first] = NodeHandle(I.second);
  }

  // Then add the node for all globals from all equivalence classes. The scalar
  // map only holds a single global for each equivalence class, so map the
  // actual global value to the node representing that equivalence class. We use
  // getNodeForValue to find the correct node since it handles this indirection.
  const EquivalenceClasses<const GlobalValue*> &EC = DSG->getGlobalECs();
  for (EquivalenceClasses<const GlobalValue*>::iterator I = EC.begin(), E = EC.end();
       I != E; ++I) {
    if (!I->isLeader()) {
      continue;
    }
    for (EquivalenceClasses<const GlobalValue*>::member_iterator MI = EC.member_begin(I), ME = EC.member_end();
         MI != ME; ++MI) {
      if (DSG->hasNodeForValue(*MI)) {
        NodeMap[*MI] = NodeHandle(DSG->getNodeForValue(*MI));
      }
    }
  }
}

void PointerEquivalenceAnalysis::appendMaskForVarArgs(ImmutableCallSite CS, SmallVectorImpl<llvm::Value *> &SV) {
  if (CS.getFunctionType()->isVarArg()) {
    // Traverse var args for this call and see if any of them have an associated
    // node. If one of the var args has a node it will be the node for all of the
    // args. If no var arg is found to have a node, we append a random mask.
    NodeHandle NH;
    for (int i = CS.getFunctionType()->getNumParams(), e = CS.getNumArgOperands();
         i < e; i++) {
      NH = getNode(CS.getArgOperand(i));
      if (NH.getNode()) {
        break;
      }
    }
    SV.push_back(getMaskForNode(NH));
  }
}

void PointerEquivalenceAnalysis::appendMasksForReachable(const Value *V, const DataLayout &DL, const FunctionWrappers &FW,
                                                         SmallVectorImpl<Value *> &SV) {
  Type *T = V->getType();
  NodeHandle N = getNode(V);
  DenseSet<StructType*> DS;
  appendMasksForReachable(T, N, DL, FW, SV, DS);
}

void PointerEquivalenceAnalysis::appendMasksForReachable(Type *T, const NodeHandle &N,
                                                         const DataLayout &DL, const FunctionWrappers &FW,
                                                         SmallVectorImpl<Value *> &SV,
                                                         DenseSet<StructType*> &Visited) {
  if (! FW.typeCanBeEncrypted(T)) {
    return;
  }

  // We pass masks for pointers that are not pointers to functions
  if (T->isPointerTy() && !T->getPointerElementType()->isFunctionTy()) {
    StructType *ST = dyn_cast<StructType>(T->getPointerElementType());

    // Only pass masks for pointer to struct type if we haven't passed masks for it already
    if (ST && Visited.count(ST)) {
      return;
    }

    // Add mask for this node
    SV.push_back(getMaskForNode(N));

    // visit children,
    if (ST) {
      // if pointer to struct, the children are any struct members that may be pointers
      Visited.insert(ST);
      const StructLayout *SL = DL.getStructLayout(ST);
      unsigned count = 0;
      for (Type::subtype_iterator i = ST->element_begin(), e = ST->element_end(); i != e; ++i, ++count) {
        appendMasksForReachable(*i, N.getLink(SL->getElementOffset(count)), DL, FW, SV, Visited);
      }
    } else if (T->getPointerElementType()->isPointerTy()) {
      // if pointer to pointer of some other type, the child will be a link at position 0
      NodeHandle Child = N.getLink(0);
      appendMasksForReachable(T->getPointerElementType(), Child, DL, FW, SV, Visited);
    }
  }
}

void SteensgaardsPEA::assignMaskRecursively(const NodeHandle &N, Constant *M, DenseSet<const DSNode*> &Visited, StringRef Reason) {
  assignMask(N, M, Reason);
  Visited.insert(N.Node);
  for (const DSNode *Child : *N.Node) {
    if (!Visited.count(Child)) {
      assignMaskRecursively(NodeHandle(Child), M, Visited, Reason);
    }
  }
}

PointerEquivalenceAnalysis::NodeHandle SteensgaardsPEA::getNode(const Value *V) {
  auto I = NodeMap.find(V);

  // If V is not present, return a default NodeHandle
  if (I == NodeMap.end()) {
    return NodeHandle();
  }
  return I->second;
}

bool SteensgaardsPEA::examineExternalCallSite(ImmutableCallSite CS) {
  bool AddedUnencrypted = false;

  // Create a collection containing the call instruction and all arguments
  SmallVector<const Value*, 8> Values;
  Values.push_back(CS.getInstruction());
  Values.append(CS.arg_begin(), CS.arg_end());

  std::string Msg;
  if (const Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
    Msg.append("Unwrapped call to: ");
    Msg.append(F->getName().str());
  } else {
    Msg.append("Indirect call to external function");
  }

  // Traverse the collection and assign the mask 0 to any nodes
  for (const Value *V : Values) {
    auto i = NodeMap.find(V);

    // If we find a value with a node in the graph, we have identified an
    // equivalence class we cannot encrypt
    if (i != NodeMap.end()) {
      assignMaskRecursively(i->second, nullMask(), Msg);
      AddedUnencrypted = true;
    }
  }

  return AddedUnencrypted;
}

static bool pairCompare(const std::pair<const Function* , int> &l, const std::pair<const Function*, int> &r) {
  return l.second < r.second;
}

void SteensgaardsPEA::handleUnwrappedExternalCalls(const std::vector<ImmutableCallSite> &ExternalCalls) {
  // Find anything that is passed to or returned from an unwrapped function and
  // assign the mask 0
  DenseMap<const Function*, int> UnwrappedFunctions;
  int IndirectNodes = 0;
  for (ImmutableCallSite CS : ExternalCalls) {
    int InitialMasks = MaskMap.size();
    if (examineExternalCallSite(CS)) {
      DEBUG(
          if (const Function *F = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts())) {
            UnwrappedFunctions[F] += MaskMap.size() - InitialMasks;
          } else {
            IndirectNodes += MaskMap.size() - InitialMasks;
          } );
    }
  }

  // Output unwrapped external functions sorted by the number of nodes they
  // force to be unencrypted.
  std::vector<std::pair<const Function*, int> > Sorted;
  DEBUG(
      Sorted.insert(Sorted.end(), UnwrappedFunctions.begin(), UnwrappedFunctions.end());
      std::sort(Sorted.begin(), Sorted.end(), pairCompare);
      errs() << "Unwrapped functions, number of nodes can't encrypt\n";
      errs() << "Indirect function calls , " << IndirectNodes << '\n';
      for (auto I = Sorted.rbegin(), E = Sorted.rend(); I != E; ++I) {
        errs() << Function::getRealLinkageName(I->first->getName()) << " , " << I->second << '\n';
      } );
}

#undef DEBUG_TYPE
