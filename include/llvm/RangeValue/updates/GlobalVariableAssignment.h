
#ifndef GLOBAL_VARIABLE_ASSIGNMENT_H
#define GLOBAL_VARIABLE_ASSIGNMENT_H

#include "StateSpaceUpdate.h"
#include "../variables/Variable.h"
#include "llvm/IR/Instructions.h"
#include <string>

using namespace std;

namespace llvm {

    class RangeValueConstraint;

    /*
     * A special update funciton to deal with global variables that can be updated from multiple threads
     * */
    class GlobalVariableAssignment : public StateSpaceUpdate {
        Variable* variable;
        Value* value;

        public:
        GlobalVariableAssignment(Variable* var, Value* val) : variable(var), value(val){}

        RangeValueLattice* update(RangeValueLattice* lattice);

        string toStr();

        static RangeValueConstraint* create(int source, int destination, Variable* variable, Value* newValue);
    };
}

#endif
