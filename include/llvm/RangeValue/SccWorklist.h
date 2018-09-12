
#ifndef SCC_WORKLIST_H
#define SCC_WORKLIST_H

#include "DebugSettings.h"

#include "Worklist.h"
#include "RangeValueConstraint.h"
#include "ConstraintCollection.h"
#include "Graph.h"
#include "ConstraintSorting.h"
#include <string>
#include <set>
#include <queue>


using namespace std;

namespace llvm {


    /*
     * */
    class SccWorklist : public Worklist {
        int evaluatedConstraintCount;
        set<RangeValueConstraint*, rvc_compare> current;

        vector<int> nodeScc;
        vector<int> nodeLocalReversePostOrder;

        map<int, set<RangeValueConstraint*, rvc_compare>> pending;
        priority_queue<int> pendinginSccs; 

        ConstraintGraph* graph;

        public:

        SccWorklist(ConstraintCollection* collection); 

        ~SccWorklist(){delete graph;}

        void clear();

        void insert(RangeValueConstraint* constraint);

        RangeValueConstraint* extract();

        bool isEmpty();

        string toStr();
    };

}


#endif
