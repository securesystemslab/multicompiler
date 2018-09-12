#ifndef STRUCT_VARIABLE_H
#define STRUCT_VARIABLE_H

#include "ContainerVariable.h"

using namespace std;

namespace llvm {

    /*
     * A variable containing all the fields of a struct as Variables
     * */
    class StructVariable : public ContainerVariable { 
        private:
            StructType* type;

        protected:
            virtual void initialize(PointerMap* pointers) override;

        public:
            StructVariable(const Value* decl, StructType* type);

            virtual ~StructVariable();

            virtual IntervalLattice getRangeValue(RangeValueLattice* lattice);

            virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue);

            virtual string toStr();

            virtual vector<Variable*> getAffectedVariables() override;

            static bool classof(const Variable* variable){
                return variable->getKind() == VK_ContainerStruct;
            }
    };
}
#endif
