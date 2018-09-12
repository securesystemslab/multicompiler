
#ifndef RANGEVALUELATTICE_H
#define RANGEVALUELATTICE_H

#include "llvm/IR/Instructions.h"
#include "IntervalLattice.h"
#include "../PointerMap.h"
#include "../DebugSettings.h"

#include <string>
#include <map>
#include <set>

using namespace std;

namespace llvm {

    class Variable;

    /*
     * The lattice mapping each known variable to an IntervalLattice
     * */
    class RangeValueLattice {
        map<Variable*, IntervalLattice> variables;
        set<Variable*>  pointedTo;

        public:
        RangeValueLattice() {}

        RangeValueLattice(RangeValueLattice* other);

        ~RangeValueLattice();

        bool leq(RangeValueLattice* lattice);

        RangeValueLattice* join(RangeValueLattice* lattice);

        IntervalLattice getVariableLattice(Variable* variable);

        void setVariableLattice(Variable* variable, IntervalLattice lattice, bool definiteValue);

        void markAsPointedTo(Variable* variable);

        bool isPointedTo(Variable* variable);

        bool isInLattice(Variable* variable);

        string toStr();

        const map<Variable*, IntervalLattice>& getIntervals();

        int size();

        static RangeValueLattice* bottom();
    };
}

#endif
