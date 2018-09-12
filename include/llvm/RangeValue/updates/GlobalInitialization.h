
#ifndef GLOBAL_INITIALIZATION_H
#define GLOBAL_INITIALIZATION_H

#include "StateSpaceUpdate.h"
#include "../variables/Variable.h"
#include <string>


using namespace std;

namespace llvm {

    class RangeValueConstraint;

    /*
     * A state update function representing the initial assignment of a variable to the extremal value (ie "TOP")
     * */
    class GlobalInitialization : public StateSpaceUpdate {
        Variable* variable;
        Constant* initializer;

        public:
        GlobalInitialization(Variable* var, Constant* init) : variable(var), initializer(init) {}

        RangeValueLattice* update(RangeValueLattice* lattice);

        string toStr();

        static RangeValueConstraint* create(int label, Variable* variable, Constant* initializer);
    };
}

#endif
