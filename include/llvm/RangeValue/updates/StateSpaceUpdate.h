
#ifndef STATESPACEUPDATE_H
#define STATESPACEUPDATE_H

#include "../lattices/RangeValueLattice.h"

using namespace std;

namespace llvm {

    /*
     * The abstract state space transfer funciton definition
     * */
    class StateSpaceUpdate {

        public:
            virtual RangeValueLattice* update(RangeValueLattice* lattice) = 0;

            virtual ~StateSpaceUpdate(){}	

            virtual string toStr() = 0;
    };
}

#endif
