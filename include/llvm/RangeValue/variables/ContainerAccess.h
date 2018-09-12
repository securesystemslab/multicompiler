
#ifndef CONTAINER_ACCESS_H
#define CONTAINER_ACCESS_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A variable abstracting pointer lookups away
     * */
    class ContainerAccess : public Variable { 

        set<Variable*> elementVariables;
        const Value *fieldIndexValue;

        vector<Variable*> getAccessedVariables(RangeValueLattice* lattice);

        bool pointsToUnknown, active;

        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        ContainerAccess(const GetElementPtrInst* decl);

        virtual ~ContainerAccess(){}

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;

        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_ContainerAccess;
        }
    };
}
 #endif
