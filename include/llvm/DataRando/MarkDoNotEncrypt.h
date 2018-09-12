//===- MarkDoNotEncrypt.h - Mark which nodes not to encrypt -*- C++ -*-----===//

#ifndef LLVM_DATARANDO_MARKDONOTENCRYPT_H
#define LLVM_DATARANDO_MARKDONOTENCRYPT_H

#include "dsa/DataStructure.h"

namespace llvm {
class BUMarkDoNotEncrypt : public BUDataStructures {
public:
  static char ID;

  BUMarkDoNotEncrypt() : BUDataStructures(ID, "mark-do-not-encrypt", "mdne.", false, true) {}

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool canEncryptCall(CallSite CS);

  bool canReplaceAddress(const Function *F);
private:
  // Collection of equivalence classes for which we cannot safely replace uses
  // of the functions' addresses with their clones' addresses. This set only
  // stores the leader of each equivalence class.
  DenseSet<const GlobalValue*> DoNotReplaceAddress;

  void findDoNotReplaceFunctions(std::vector<CallSite> &Calls);

  void markCallDoNotEncrypt(CallSite CS);

  void specialCases(CallSite CS);

  // Propogate DoNotEncrypt marker to all nodes reachable from any node with the
  // DoNotEcryptReachable marker set.
  void propogateDoNotEncrypt(Module &M);
};

} // namespace llvm

#endif /* LLVM_DATARANDO_MARKDONOTENCRYPT_H */
