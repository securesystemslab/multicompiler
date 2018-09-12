
#ifndef ARRAY_VARIABLE_H
#define ARRAY_VARIABLE_H

#include "ContainerVariable.h"

using namespace std;

namespace llvm {

    /*
     * A variable representing an array
     * */
    class ArrayVariable : public ContainerVariable { 
        private: 
        ArrayType* type;

        // TODO: How to deal with dynamic arrays?
        protected:
        virtual void initialize(PointerMap* pointers) override;

        public:
        ArrayVariable(const Value* decl, ArrayType* type);

        virtual ~ArrayVariable();

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;

        virtual vector<Variable*> getAffectedVariables() override;

        static bool classof(const Variable* variable){
            return variable->getKind() == VK_ContainerArray;
        }
    };
}
 #endif
