
#ifndef POINTER_VARIABLE_H
#define POINTER_VARIABLE_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A variable abstracting pointer lookups away
     * */
    class PointerVariable : public Variable { 

        set<Variable*> accessedVariablesCache, elementVariables;

        void generateAccessedVariablesCache();

        bool pointsToUnknown, active;

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        PointerVariable(const Value* decl);

        virtual ~PointerVariable(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;

        virtual vector<Variable*> getAffectedVariables() override;

        set<Variable*> getElements();

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Pointer;
        }
    };
}
 #endif
