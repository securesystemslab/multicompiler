
#ifndef UNKNOWN_CONSTANT_H
#define UNKNOWN_CONSTANT_H

#include "llvm/IR/Constants.h"

#include "Variable.h"


using namespace std;

namespace llvm {

    /*
     * A catch-all variable dealing with unknown/unmodelled variables as a constant (TOP)
     * */
    class UnknownConstant : public Variable { 

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        UnknownConstant(const Value* decl); 

        virtual ~UnknownConstant(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;
        
        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Unknown;
        }
    };
}
 #endif
