
#ifndef RANGEVALUECONSTRAINT_H
#define RANGEVALUECONSTRAINT_H

#include "DebugSettings.h"

#include "updates/StateSpaceUpdate.h"
#include "lattices/RangeValueLattice.h" 
#include <string>
using namespace std;

namespace llvm {

    const int EMPTY_LABEL = -1;


    /*
     * The constraint of the form:
     * 
     * LHS =] transfer(RHS)
     *
     * That is, the new state resulting from the transfer function applied to the right-hand side is a subset of the left-hand side.
     * */
    class RangeValueConstraint {

        StateSpaceUpdate* updateFun;

        public:
        int lhs, rhs;	
        int evaluationCount;

        RangeValueConstraint (int lhs, int rhs, StateSpaceUpdate* updateFun);

        ~RangeValueConstraint();

        RangeValueLattice* evaluate(RangeValueLattice* lattice[]);	

        string toStr();    

        static bool compare(const RangeValueConstraint* x, const RangeValueConstraint* y);

        static RangeValueConstraint* createInformationForwarding(int source, int destination);
    };
}

#endif
