
#ifndef INTEGER_VARIABLE_H
#define INTEGER_VARIABLE_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A variable representing a single integer
     * */
    class IntegerVariable : public Variable { 

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        IntegerVariable(const Value* decl); 

        virtual ~IntegerVariable(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;
        
        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Integer;
        }
    };
}
 #endif
