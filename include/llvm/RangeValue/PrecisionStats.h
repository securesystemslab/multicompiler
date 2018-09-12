#ifndef PRECISION_STATS_H
#define PRECISION_STATS_H

#include "DebugSettings.h"
#include "lattices/RangeValueLattice.h"
#include "ConstraintCollection.h"
#include "llvm/CrititcalValue/CriticalValue.h"

class PrecisionStats {

    private:
        int boundedIntervalsCount = 0;
        int partiallyBoundedIntervalsCount = 0;

        Function* currentFunction = NULL;

        int neverPreciselyAssigned = 0;
        int criticalValueCount = 0;
        int arithmeticCount = 0;
        int constantCount = 0;
        int unknownValueCount = 0;
        int unknownConstantCount = 0;
        int arrayFieldCount = 0;
        int pointedToCount = 0;
        int unknownPointerElementIndex = 0;
        int pointerSelfReferenceCount = 0;

        int unknownPrecisionLossCount = 0;
        map<Variable::VariableKind, int> unknownPrecisionLossTypes;
        map<Variable::VariableKind, int> criticalValuesTypes;

        Value* currentControlFlowInst = NULL;
        RangeValueLattice** analysisResult;
        ConstraintCollection* constraintsCollection;
        CriticalValue* critVal;
        
    public: 
        PrecisionStats(RangeValueLattice** analysis, ConstraintCollection* constraints, CriticalValue* criticalValue)
            : analysisResult(analysis), constraintsCollection(constraints), critVal(criticalValue) {}

        void printStats();
};

#endif
