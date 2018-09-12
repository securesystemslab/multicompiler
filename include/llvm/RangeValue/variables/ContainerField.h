

#ifndef CONTAINER_FIELD_H
#define CONTAINER_FIELD_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A wrapper for a field in a container that maintains the relation between parent container and fields
     * */
    class ContainerField : public Variable{
        protected:
        Variable *parent, *field;

        int subFieldIndex;

        bool isCollectionElement;

        virtual ~ContainerField();
        
        virtual void initialize(PointerMap* pointers);

        public:
        ContainerField(Variable* parent, Variable* field, int fieldIndex, bool isCollectionElement = false); 

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) override;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) override;

        virtual string toStr() override;

        virtual vector<Variable*> getAffectedVariables() override;

        Variable* getField();
                
        static bool classof(const Variable* variable){
            return variable->getKind() == VK_Field;
        }
    };

}

#endif
