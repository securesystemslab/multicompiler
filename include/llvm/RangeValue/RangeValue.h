
#ifndef RANGE_VALUE_H
#define RANGE_VALUE_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ValueMap.h"

#include "llvm/Analysis/TargetLibraryInfo.h"

#include "llvm/CrititcalValue/CriticalValue.h"

namespace llvm{

    struct RangeValue : public ModulePass{

        private:
            ValueMap<Value*, long> positiveBitMask;
            ValueMap<Value*, long> negativeBitMask;
        public:
        static char ID;

        RangeValue() : ModulePass(ID) {}

        bool hasPositiveBitMask(Value* value){
            return positiveBitMask.find(value) != positiveBitMask.end();
        }
        bool hasNegativeBitMask(Value* value){
            return negativeBitMask.find(value) != negativeBitMask.end();
        }

        long getPositiveBitMask(Value* value){
            return positiveBitMask[value];
        }

        long getNegativeBitMask(Value* value){
            return negativeBitMask[value];
        }

        bool runOnModule(Module &mod) override; 

        virtual void getAnalysisUsage(AnalysisUsage& AU) const override {
            AU.setPreservesAll();
            AU.addRequired<CriticalValue>();
            AU.addRequired<TargetLibraryInfoWrapperPass>();
        }

    };
}


#endif
