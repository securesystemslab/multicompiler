
#ifndef CONSTRAINTSOLVER_H
#define CONSTRAINTSOLVER_H

#include "DebugSettings.h"

#include "RangeValueConstraint.h"
#include "ConstraintCollection.h"
#include "Worklist.h"
#include "lattices/RangeValueLattice.h"

#include <vector>

using namespace std;

namespace llvm{

    RangeValueLattice** solveConstraints(ConstraintCollection* constraints, Worklist* worklist);

    RangeValueLattice** solveConstraints(ConstraintCollection* constraints);
}

#endif
