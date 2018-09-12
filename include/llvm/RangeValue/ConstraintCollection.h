
#ifndef CONSTRAINTGENERATOR_H
#define CONSTRAINTGENERATOR_H

#include "DebugSettings.h"

#include "variables/Variable.h"
#include "PointerMap.h"
#include "RangeValueConstraint.h"
#include "updates/ExtremalValueAssignment.h"
#include "updates/VariableAssignment.h"
#include "updates/GlobalVariableAssignment.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"

#include <vector>
#include <map>
#include <set>

using namespace std;

namespace llvm{

    class ConstraintCollection {

        set<Variable*> localVariables;
        set<Variable*> arguments;
        set<Variable*> globalVariables;
        vector<RangeValueConstraint*> constraints; 
        map<int, vector<RangeValueConstraint*>> constraintsForLabel;
        map<Instruction*, int> instLabels;
        map<Value*, int> globalValueLabels;
        int lastLabel;

        void createConstraint(vector<RangeValueConstraint*>* list, int source, int destination, Instruction* inst);

        bool definesNonEmptyConstraint(Instruction* inst);

        void addConstraints(vector<RangeValueConstraint*> constraints);

        void addConstraintsForFunction(Function& fun);

        int getGlobalLabel(Value* globalVar);

        public:

        ConstraintCollection(Module& mod);

        ~ConstraintCollection();

        int getLabelCount();

        vector<RangeValueConstraint*> getConstraints(int label);

        vector<RangeValueConstraint*> getAll();

        set<Variable*> getLocalVariables();

        set<Variable*> getGlobalVariables();

        set<Variable*> getArgumentVariables();

        map<Instruction*, int> getInstructionMap();
    };
}

#endif
