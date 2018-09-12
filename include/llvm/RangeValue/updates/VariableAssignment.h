#ifndef VARIABLEASSIGNMENT_H
#define VARIABLEASSIGNMENT_H

#include "StateSpaceUpdate.h"
#include "../variables/Variable.h"
#include "llvm/IR/Instructions.h"
#include <string>

using namespace std;

namespace llvm {

    class RangeValueConstraint;

    /*
     * The transfer functions for variable assignments
     * */
    class VariableAssignment : public StateSpaceUpdate {
        Variable* variable;
        Value* value;

        public:
        VariableAssignment(Variable* var, Value* val) : variable(var), value(val){}

        RangeValueLattice* update(RangeValueLattice* lattice);

        string toStr();

        static RangeValueConstraint* create(int source, int destination, Variable* variable, Value* newValue);
    };
}

#endif
