//===- DataRandomizer.h - Randomize data values -----------------*- C++ -*-===//

#ifndef LLVM_DATARANDO_DATARANDOMIZER_H
#define LLVM_DATARANDO_DATARANDOMIZER_H

#include "llvm/DataRando/Runtime/DataRandoTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/TypeBuilder.h"
#include "llvm/IR/ValueMap.h"

namespace llvm {
class Type;
class Value;
class DataLayout;
class PointerEquivalenceAnalysis;
class FunctionWrappers;

class DataRandomizer {
public:
  explicit DataRandomizer(const DataLayout &D, LLVMContext &C)
      : DL(D), MaskTy(TypeBuilder<mask_t, false>::get(C)) {}

  explicit DataRandomizer(const Module &M) : DataRandomizer(M.getDataLayout(), M.getContext()) {}

  bool instrumentMemoryOperations(Module &M, PointerEquivalenceAnalysis &PEA, ValueMap<Value*, Value*>* decryptedInstructions);

  bool instrumentMemoryOperations(Function &F, PointerEquivalenceAnalysis &PEA, ValueMap<Value*, Value*>* decryptedInstructions);

  bool wrapLibraryFunctions(Module &M, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW);

  bool wrapLibraryFunctions(Function &F, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW);

  bool encryptMainArgs(Module &M, PointerEquivalenceAnalysis &PEA, FunctionWrappers &FW);

  bool encryptGlobalVariables(Module &M, PointerEquivalenceAnalysis &PEA);

  Value *createXor(IRBuilder<> &builder, Value *V, Value *Address, Value *Mask, unsigned Alignment = 1);

private:
  // Get the equivalent width integer type
  Type *getIntType(Type *T);

  Value *createCast(IRBuilder<> &builder, Value *V, Type *T);

  Value *createAddressAlignment(IRBuilder<> &builder, Value *Address, uint64_t MaskSize, uint64_t Alignment);

  Value *getEffectiveMask(IRBuilder<> &builder, Value *Address, Value *Mask, unsigned Alignment);

  Value *getMaskAsType(IRBuilder<> &builder, Type *Ty, Value *Mask);

  const DataLayout &DL;
  Type *MaskTy;
};


}

#endif /* LLVM_DATARANDO_DATARANDOMIZER_H */
