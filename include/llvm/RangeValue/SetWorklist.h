
#ifndef SETWORKLIST_H
#define SETWORKLIST_H

#include "DebugSettings.h"

#include "Worklist.h"
#include "RangeValueConstraint.h"
#include <string>
#include <set>


using namespace std;

namespace llvm {


    /*
     * A simple set based implementation of the Worklist
     * */
    class SetWorklist : public Worklist {
        int evaluatedConstraintCount;
        set<RangeValueConstraint*, rvc_compare> constraints;

        public:

        SetWorklist() : evaluatedConstraintCount(0) {}

        ~SetWorklist(){}

        void clear();

        void insert(RangeValueConstraint* constraint);

        RangeValueConstraint* extract();

        bool isEmpty();

        string toStr();
    };

}


#endif
