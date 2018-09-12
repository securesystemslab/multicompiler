#ifndef CRITICALVALUE_H
#define CRITICALVALUE_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ValueMap.h"


namespace llvm{

    struct CriticalValue : public ModulePass{

        static char ID;

        ValueMap<Value*, bool> visited;

        ValueMap<Value*, std::vector<Value*>> controlFlowVariables;

        CriticalValue() : ModulePass(ID) {}

        void markControlFlowVariables(Value* inst, Value* operand);

        public: 

        virtual bool runOnModule(Module& mod);

        bool doesVariableAffectControlFlow(Value* inst);

        const std::vector<Value*> getControlFlowAffectingOperands(Value* inst);

        virtual void getAnalysisUsage(AnalysisUsage& AU) const override {
            AU.setPreservesAll();
        }
    };

}



#endif
