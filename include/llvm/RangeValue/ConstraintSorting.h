
#ifndef CONSTRAINT_SORTING_H
#define CONSTRAINT_SORTING_H

#include "ConstraintCollection.h"
#include "Graph.h"

#include <map>
#include <set>
#include <vector>
#include <stack>
#include <utility>
#include <algorithm>

using namespace std;

namespace llvm{

    class ConstraintSorting{
        private:
            pair<vector<int>,vector<int>> constraintSorting;

        public:
            ConstraintSorting(ConstraintGraph* graph);

            pair<vector<int>,vector<int>> getSCCConstraintSorting() { return constraintSorting; }
    };

    class ReversePostOrderSorting {

        private:
            ConstraintGraph* graph;
            int index;
            set<uint> nodes;
            vector<bool> visited;
            map<int, int> reversePostOrderSorting;

            void depthFirstTraversal(int nodeIndex); 
        public:

            ReversePostOrderSorting(ConstraintGraph* g, set<uint> nodes);
            map<int, int> getReversePostOrderSorting();
    };

    template <class T>
        class StronglyConnectedComponentCalculator {

            private:
                struct AuxData {
                    int index;
                    int lowIndex; 
                    bool onStack;

                    AuxData() : index(-1), lowIndex(-1), onStack(false){}
                };


                Graph<T>* graph;
                int index; 
                vector<set<uint>> sccs;
                vector<AuxData> auxData;
                stack<uint> nodeStack;

                void strongConnect(uint node);

            public:
                StronglyConnectedComponentCalculator(Graph<T>* graph);

                vector<set<uint>> getSCCsInReverseTopologicalOrder() { return sccs; }
        };
}
#endif
