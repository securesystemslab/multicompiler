
#ifndef MALLOC_VARIABLE_H
#define MALLOC_VARIABLE_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A variable abstracting pointer lookups away
     * */
    class MallocVariable : public Variable { 

        Type* underlyingType;
        Variable* underlyingTypeVar;

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        MallocVariable(const Value* decl);

        virtual ~MallocVariable();

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;

        virtual vector<Variable*> getAffectedVariables() override;

        Variable* getUnderlyingTypeVar();

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Malloc;
        }
    };
}
 #endif
