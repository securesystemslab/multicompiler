
#ifndef PHI_NODE_VARIABLE_H
#define PHI_NODE_VARIABLE_H

#include "Variable.h"
#include <set>

using namespace std;

namespace llvm {

    /*
     * A variable representing the LUP of all incoming values for the phi node
     * */
    class PhiNodeVariable : public Variable { 

        set<Variable*> incoming;
        bool active;

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        PhiNodeVariable(const PHINode* decl);

        virtual ~PhiNodeVariable(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice);

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue);

        virtual string toStr();

        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Pointer;
        }
    };
}
 #endif
