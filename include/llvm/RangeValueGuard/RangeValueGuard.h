#ifndef RANGE_VALUE_GUARD_H
#define RANGE_VALUE_GUARD_H

#include "llvm/Pass.h"
#include "llvm/DataRando/DataRando.h"
#include "llvm/CrititcalValue/CriticalValue.h"
#include "llvm/RangeValue/RangeValue.h"

namespace llvm {

    struct RangeValueGuard : ModulePass {
        private:

            void insertGuard(RangeValue* rangeValue, Instruction* instToGuard, Instruction* decruptedInst, BasicBlock* errorBlock);

            bool isIntOrIntPtrType(Type* type);

            BasicBlock* createErrorBlock(BasicBlock* insertBefore);
        public:
            static char ID;

            RangeValueGuard() : ModulePass(ID) {}

            bool runOnModule(Module &mod) override; 

            virtual void getAnalysisUsage(AnalysisUsage& AU) const override {
                AU.addRequired<CriticalValue>();
                AU.addRequired<RangeValue>();
                AU.addRequired<DataRando>();
                AU.addPreserved<CriticalValue>();
                AU.addPreserved<RangeValue>();
                AU.addPreserved<DataRando>();
            }
    };
}
#endif
