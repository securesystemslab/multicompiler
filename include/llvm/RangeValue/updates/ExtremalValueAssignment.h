
#ifndef EXTREMALVALUEASSIGNMENT_H
#define EXTREMALVALUEASSIGNMENT_H

#include "StateSpaceUpdate.h"
#include "../variables/Variable.h"
#include <string>


using namespace std;

namespace llvm {

    class RangeValueConstraint;

    /*
     * A state update function representing the initial assignment of a variable to the extremal value (ie "TOP")
     * */
    class ExtremalValueAssignment : public StateSpaceUpdate {
        Variable* variable;
        bool pointedTo;

        public:
        ExtremalValueAssignment(Variable* var, bool pt) : variable(var), pointedTo(pt) {}

        RangeValueLattice* update(RangeValueLattice* lattice);

        string toStr();

        static RangeValueConstraint* create(int label, Variable* variable, bool pointedTo = false);
    };
}

#endif
