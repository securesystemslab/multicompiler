
#ifndef CONSTANT_INTEGER_H
#define CONSTANT_INTEGER_H

#include "llvm/IR/Constants.h"

#include "Variable.h"


using namespace std;

namespace llvm {

    /*
     * A variable representing a constant integer
     * */
    class ConstantInteger : public Variable { 

        private:
            long constantValue;

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        ConstantInteger(const ConstantInt* decl); 

        ConstantInteger(const Value* decl, long constantValue); 

        virtual ~ConstantInteger(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;
        
        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Constant;
        }
    };
}
 #endif
