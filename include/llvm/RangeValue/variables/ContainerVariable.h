
#ifndef CONTAINER_VARIABLE_H
#define CONTAINER_VARIABLE_H

#include "Variable.h"

using namespace std;

namespace llvm {

    /*
     * A super class for all variables acting as containers for other variables (arrays, structs)
     * */
    class ContainerVariable : public Variable{
        protected:

        vector<Variable*> subFields;
        int fieldCount;

        public:
        ContainerVariable(const Value* decl, VariableKind k) : Variable(decl, k){}

        virtual ~ContainerVariable() {}

        Variable* getSubField(uint index); 

        vector<Variable*> getSubFields();

        int getFieldCount();
        
        static bool classof(const Variable* variable){
            return variable->getKind() >= VK_Container &&
                   variable->getKind() <= VK_LastContainer;
        }
    };

}

#endif
