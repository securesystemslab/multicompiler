//===- DataRandomizer.cpp - Randomize data values -------------------------===//

#include "llvm/DataRando/DataRandomizer.h"
#include "llvm/DataRando/PointerEquivalenceAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

using namespace llvm;

static cl::opt<bool> AlwaysEmitMaskAlignment("always-emit-mask-alignment", cl::desc("Always output code to align the mask regardless of the specified alignment of the instruction"), cl::init(false));

Type *DataRandomizer::getIntType(Type *T) {
  if (T->isPtrOrPtrVectorTy()) {
    return DL.getIntPtrType(T);
  }

  if (VectorType *VecTy = dyn_cast<VectorType>(T)) {
    return VectorType::getInteger(VecTy);
  }

  assert(T->isSized() && "Unable to determine the size of T");
  return Type::getIntNTy(T->getContext(), DL.getTypeSizeInBits(T));
}

Value *DataRandomizer::createAddressAlignment(IRBuilder<> &builder, Value *Address, uint64_t MaskSize, uint64_t Alignment) {
  Type *ElementType = Address->getType()->getPointerElementType();
  Type *AddressIntType = getIntType(Address->getType());
  Type *ResultIntType = ElementType->isVectorTy() ? VectorType::get(AddressIntType, ElementType->getVectorNumElements()) : AddressIntType;


  // Find the effective address to use for the alignment computation.
  Value *EffectiveAddress;
  if (ElementType->isVectorTy()) {

    // Create a vector of offsets for the addresses of elements of the vector.
    SmallVector<Constant *, 4> V;
    unsigned VectorElementSize = DL.getTypeAllocSize(ElementType->getVectorElementType());
    unsigned offset = 0;
    for (unsigned i = 0; i < ElementType->getVectorNumElements(); i++) {
      V.push_back(ConstantInt::get(AddressIntType, offset));
      offset += VectorElementSize;
    }
    Value *OffsetVector = ConstantVector::get(V);

    if (Alignment % MaskSize == 0 && !AlwaysEmitMaskAlignment) {
      // We can do the alignment of the masks without worrying about the actual
      // address. The resulting code will be able to be constant folded.
      EffectiveAddress = OffsetVector;
    } else {
      // Create a vector of the address repeated
      Value *AddressVector = UndefValue::get(ResultIntType);
      Value *AddressInt = createCast(builder, Address, AddressIntType);
      for (unsigned i = 0; i < ElementType->getVectorNumElements(); i++) {
        AddressVector = builder.CreateInsertElement(AddressVector, AddressInt, i);
      }

      // Add the offsets for the effective addresses of elements of the vector.
      EffectiveAddress = builder.CreateAdd(AddressVector, OffsetVector);
    }
  } else {
    // We are not pointing at a vector, so we can use the address directly, just
    // cast it to the appropriate type.
    EffectiveAddress = createCast(builder, Address, ResultIntType);
  }

  Value *ShiftByBytes = builder.CreateURem(EffectiveAddress, ConstantInt::get(ResultIntType, MaskSize));
  Value *ShiftByBits = builder.CreateMul(ShiftByBytes, ConstantInt::get(ResultIntType, 8)); // 8 bits per byte

  return ShiftByBits;
}

// Emit any code necessary to cast the mask to the desired integer or integer
// vector type.
Value *DataRandomizer::getMaskAsType(IRBuilder<> &builder, Type *Ty, Value *Mask) {
  assert(Ty->isIntOrIntVectorTy()
         && "Ty must be integer type or vector of integer type");
  assert(Mask->getType() == MaskTy && "Incorrect mask type");

  if (Ty->isIntegerTy()) {
    // If the integer type is larger than the native mask size, create code to
    // make the mask larger.
    if (Ty->getScalarSizeInBits() > Mask->getType()->getScalarSizeInBits()) {
      Value *Result = builder.CreateZExt(Mask, Ty);
      // Repeatedly Shl and OR to make a mask of a larger type.
      for (unsigned i = Mask->getType()->getScalarSizeInBits(), e =  Ty->getScalarSizeInBits();
           i < e; i *= 2) {
        Value *Shl = builder.CreateShl(Result, i);
        Result = builder.CreateOr(Result, Shl);
      }
      return Result;
    }
    // Otherwise we can just cast to the type.
    return builder.CreateTruncOrBitCast(Mask, Ty);
  }

  // else we have a vector of integers
  VectorType *VecTy = cast<VectorType>(Ty);
  unsigned NumElements = VecTy->getNumElements();

  // Cast the mask to the element type, if Mask is a constant, IRBuilder will
  // create a ConstantExpr.
  Value *CastMask = getMaskAsType(builder, VecTy->getElementType(), Mask);

  if (Constant *ConstantMask = dyn_cast<Constant>(CastMask)) {
    SmallVector<Constant*, 4> V;
    for (unsigned i = 0; i < NumElements; i++) {
      V.push_back(ConstantMask);
    }
    return ConstantVector::get(V);
  }

  // else the mask is not a constant, we must create a vector containing it
  Value *MaskVector = UndefValue::get(VecTy);
  for (unsigned i = 0; i < NumElements; i++) {
    MaskVector = builder.CreateInsertElement(MaskVector, CastMask, i);
  }
  return MaskVector;
}

namespace {

// Determine the type used for the mask in intermediate computations for a
// target value with type ValueIntType. Don't choose a type that will truncate
// the mask, so don't choose a type smaller than MaskSizeInBytes.
Type *getIntermediateMaskType(Type *ValueIntType, uint64_t MaskSizeInBytes) {
  assert(ValueIntType->isIntOrIntVectorTy()
         && "ValueIntType must be integer type or vector of integer type");
  uint64_t MaskSizeInBits = MaskSizeInBytes * 8;
  if (ValueIntType->isVectorTy()) {
    Type *ElementMaskType = getIntermediateMaskType(ValueIntType->getVectorElementType(), MaskSizeInBytes);
    return VectorType::get(ElementMaskType, ValueIntType->getVectorNumElements());
  }

  if (ValueIntType->getPrimitiveSizeInBits() > MaskSizeInBits) {
    return ValueIntType;
  }

  return IntegerType::get(ValueIntType->getContext(), MaskSizeInBits);
}

}

Value *DataRandomizer::getEffectiveMask(IRBuilder<> &builder, Value *Address, Value *Mask, unsigned Alignment) {
  assert(Mask->getType() == MaskTy && "Incorrect mask type");
  const uint64_t MaskSize = PointerEquivalenceAnalysis::EffectiveMaskSize;
  Type *ValueType = Address->getType()->getPointerElementType();
  uint64_t ValueSize = DL.getTypeStoreSize(ValueType);
  Type *ValueIntType = getIntType(ValueType);
  Value *RealMask;
  if (Alignment == 0) {
    // If the alignment is 0 it is assumed to be the ABI alignment for the type.
    Alignment = DL.getABITypeAlignment(ValueType);
  }

  // If the alignment is less than the store size of the type, or the size of
  // the mask is not evenly divisible by the size of the type, then the mask
  // will need to be rotated since the access could straddle the mask alignment.
  bool NeedRotate = (Alignment < ValueSize) || (MaskSize % ValueSize);


  // If access is unaligned, or if it is accessing a vector where the elements
  // aren't aligned to the size of the mask we have to emit code to align the
  // masks.
  if (Alignment % MaskSize ||
      (ValueIntType->isVectorTy() && DL.getTypeAllocSize(ValueIntType->getVectorElementType()) % MaskSize) ||
      AlwaysEmitMaskAlignment) {
    // rotate mask to the correct alignment

    Type *MaskType = getIntermediateMaskType(ValueIntType, MaskSize);
    Value *MaskVal = getMaskAsType(builder, MaskType, Mask);
    Value *ShiftByBits = createAddressAlignment(builder, Address, MaskSize, Alignment);
    Value *ShiftByBitsCast = builder.CreateZExtOrTrunc(ShiftByBits, MaskType);


    // Perform the bitwise shift or right rotate operation
    Value *Result;
    Value *Shr = builder.CreateLShr(MaskVal, ShiftByBitsCast);
    if (NeedRotate) {
      Value *Sub = builder.CreateSub(ConstantInt::get(MaskType, 0), ShiftByBitsCast);
      Value *And = builder.CreateAnd(Sub, DL.getTypeStoreSizeInBits(MaskTy) - 1);
      Value *Shl = builder.CreateShl(MaskVal, And);
      Value *Or = builder.CreateOr(Shl, Shr);
      Result = Or;
    } else {
      Result = Shr;
    }
    // We have shifted the mask to handle alignment, now cast to the proper
    // type. We use TruncOrBitCast since the data randomization will only work
    // correctly if the type of the value is smaller than the type of the mask.
    // If we were to zero extend the mask then we wouldn't be encrypting part of
    // the value.
    RealMask = builder.CreateTruncOrBitCast(Result, ValueIntType);
  } else {
    // Access is properly aligned, we can use the mask directly
    RealMask = getMaskAsType(builder, ValueIntType, Mask);
  }
  return RealMask;
}

Value *DataRandomizer::createXor(IRBuilder<> &builder, Value *V, Value *Address, Value *Mask, unsigned Alignment) {
  assert(Address->getType()->isPointerTy() && "Address doesn't have pointer type");
  assert(Address->getType()->getPointerElementType() == V->getType() && "Address doesn't point to the type of V");
  assert(Mask->getType() == MaskTy && "Incorrect mask type");

  Value *RealMask = getEffectiveMask(builder, Address, Mask, Alignment);

  // Cast to int
  Value *cast = createCast(builder, V, RealMask->getType());

  // xor the value
  Value *xorInst = builder.CreateXor(cast, RealMask);

  // Cast back to original type
  return createCast(builder, xorInst, V->getType());
}

Value *DataRandomizer::createCast(IRBuilder<> &builder, Value *V, Type *T) {
  if (V->getType()->isPtrOrPtrVectorTy()) {
    assert(T->isIntOrIntVectorTy());
    return builder.CreatePtrToInt(V, T);
  }

  if (T->isPtrOrPtrVectorTy()) {
    assert(V->getType()->isIntOrIntVectorTy());
    return builder.CreateIntToPtr(V, T);
  }

  return builder.CreateBitCast(V, T);
}
