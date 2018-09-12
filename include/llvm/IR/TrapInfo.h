//===- TrapInfo.h - TexTrap Information -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a number of light weight data structures used
// to describe and track debug location information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TRAPINFO_H
#define LLVM_IR_TRAPINFO_H

#include "llvm/IR/Metadata.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

  class LLVMContext;
  class raw_ostream;
  class MDNode;
  class Constant;
  class GlobalVariable;

  /// TrapInfo - Trap information.  This is carried by Instruction, SDNode,
  /// and MachineInstr to propagate textrap info.
  class TrapInfo {
    friend struct DenseMapInfo<TrapInfo>;
    TrackingMDNodeRef Info;

    bool methodPointer;

  protected:
    TrapInfo(Metadata *TI) : Info((MDNode*) TI), methodPointer(false) {}

  public:
    TrapInfo() : Info(NULL), methodPointer(false) {}
    TrapInfo(TrapInfo &&X) : Info(std::move(X.Info)), methodPointer(X.methodPointer) {}
    TrapInfo(const TrapInfo &X) : Info(X.Info), methodPointer(X.methodPointer) {}
    TrapInfo &operator=(TrapInfo &&X) {
      Info = std::move(X.Info);
      methodPointer = X.methodPointer;
      return *this;
    }
    TrapInfo &operator=(const TrapInfo &X) {
      Info = X.Info;
      methodPointer = X.methodPointer;
      return *this;
    }

    /// \brief Check whether this has a trivial destructor.
    bool hasTrivialDestructor() const { 
      return !isValid(Info) || !MetadataTracking::isReplaceable(*Info);
    }

    /// get - Get a new TrapInfo that corresponds to the specified class
    static TrapInfo getVCall(GlobalVariable* ClassName, uint64_t MaxNumVFuncs);
    static TrapInfo getMethodPointer(GlobalVariable* ClassName, uint64_t MaxNumVFuncs);
    TrapInfo replaceClassName(GlobalVariable* NewClass);

    /// isUnknown - Return true if this is an unknown location.
    bool isUnknown() const { return !Info; }

    const GlobalVariable *getClassName() const;
    const Metadata *getClassMD() const;
    uint64_t getMaxNumVFuncs() const;
    bool isMethodPointer() const { return methodPointer; }

    void setMethodPointer(bool isMethodPointer) { methodPointer = isMethodPointer; };

    void replaceOperandWith(Value *From, Value *To);

    explicit operator bool() const { return Info; }

    // probably don't need this:
    /// getAsMDNode - This method converts the compressed TrapInfo node into a
    /// DIInfoation compatible MDNode.
    // MDNode *getAsMDNode() const;
    // MDNode *getAsMDNode(LLVMContext &) const { return getAsMDNode(); }

    bool operator==(const TrapInfo &TI) const {
      return Info == TI.Info && methodPointer == TI.methodPointer;
    }
    bool operator!=(const TrapInfo &TI) const {return !(*this == TI); }

    void dump() const;
    void dump(const LLVMContext &) const { dump(); }
  private:
    static bool isValid(MDNode *MD) {
      return MD &&
             MD != DenseMapInfo<Metadata *>::getEmptyKey() &&
             MD != DenseMapInfo<Metadata *>::getTombstoneKey();
    }
  };

// Specialize DenseMapInfo for TrapInfo.
template<>
struct DenseMapInfo<TrapInfo> {
  static inline TrapInfo getEmptyKey() {
    return TrapInfo(DenseMapInfo<Metadata*>::getEmptyKey());
  }
  static inline TrapInfo getTombstoneKey() {
    return TrapInfo(DenseMapInfo<Metadata*>::getTombstoneKey());
  }
  static unsigned getHashValue(const TrapInfo &TI) {
    return DenseMapInfo<Metadata*>::getHashValue(TI.Info.get()) ^
      DenseMapInfo<unsigned>::getHashValue(TI.methodPointer);
  }
  static bool isEqual(const TrapInfo &LHS,
                      const TrapInfo &RHS) {
    return LHS.Info == RHS.Info && LHS.methodPointer == RHS.methodPointer;
  }
};

} // end namespace llvm

#endif /* LLVM_IR_TRAPINFO_H */
