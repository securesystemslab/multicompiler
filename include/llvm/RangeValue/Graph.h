#ifndef GRAPH_H
#define GRAPH_H

#include "ConstraintCollection.h"

#include <map>
#include <set>
#include <vector>

using namespace std;

namespace llvm{

    
    template <class Tnode>
    class Graph;

    typedef Graph<RangeValueConstraint*> ConstraintGraph;

    template <class Tnode>
    class Graph  {

        private:
            uint nextNode;
            map<Tnode, uint> nodes;
            vector<set<uint>> edges;

            Graph() : nextNode(0){}

            void addNode(Tnode constraint){
                nodes[constraint] = nextNode++;
            }

            void addEdge(uint from, uint to){
                if (edges.size() <= from){
                    edges.resize(from + 1);
                }

                edges[from].insert(to);
            }

        public:

            uint getSize() { return nodes.size(); }

            uint getNodeIndex(Tnode constraint){ return nodes[constraint]; }

            set<uint> getEdges(uint node) { return edges[node]; }

            static ConstraintGraph* constructConstraintGraph(ConstraintCollection* constraints){
                Graph<RangeValueConstraint*>* graph = new Graph<RangeValueConstraint*>();
                for (RangeValueConstraint* constraint : constraints->getAll()){
                    graph->addNode(constraint);    
                }

                for (RangeValueConstraint* constraint : constraints->getAll()){
                    int to = graph->getNodeIndex(constraint);
                    for (RangeValueConstraint* referenced : constraints->getConstraints(constraint->rhs)){
                        int from = graph->getNodeIndex(referenced);
                        graph->addEdge(from, to);
                    }
                }

                return graph;
            }
    };
}
#endif
