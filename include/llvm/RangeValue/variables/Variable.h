
#ifndef VARIABLE_H
#define VARIABLE_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "../PointerMap.h"
#include "../lattices/RangeValueLattice.h"

#include <string>
#include <vector>
#include <map>

using namespace std;

namespace llvm {

    class RangeValueLattice;

    /*
     * Describes a location for a single variable in memory
     * */
    class Variable {
        /* Discriminator for LLVM-style RTTI*/
        public:
        enum VariableKind {
            VK_Integer,
            VK_Pointer,
            VK_PhiNode,
            VK_Constant,
            VK_Unknown,
            VK_Container,
            VK_ContainerArray,
            VK_ContainerStruct,
            VK_LastContainer,
            VK_ContainerAccess,
            VK_Field,
            VK_Malloc
        };

        private:
        // The value declaring this Variable
        const Value* declaration;
        Variable* index;
        const VariableKind kind;

        static map<const Value*, Variable*> variables;
        static PointerMap* pointerMap;

        // Gets the variable for a given value (may be NULL)
        static void addVariable(const Value* value);

        protected:
        Variable(const Value* decl, VariableKind k) : declaration(decl), index(this), kind(k){}

        set<Variable*> getVariablesPointedFrom(const Value* value, PointerMap* pointers);
        
        static Variable* getVariableForType(const Value* value, Type* type);

        public:

        static TargetLibraryInfo* TLI;

        virtual ~Variable() {}

        VariableKind getKind() const { return kind; }
        
        virtual void initialize(PointerMap* pointers) = 0;

        virtual IntervalLattice getRangeValue(RangeValueLattice* lattice) = 0;

        virtual void setRangeValue(RangeValueLattice* lattice, const Value* value, bool definiteValue) = 0;

        virtual string toStr() = 0;

        virtual vector<Variable*> getAffectedVariables() = 0;

        Variable* getIndex();

        void setIndex(Variable* newIndex);

        const Value* getDeclaration();

        bool isGlobal();
        
        // Clears all stores variables
        static void clear();

        static Variable* getVariable(const Value* value);

        static void createVariables(Module& module);

        static bool isVariable(const Value* value);

        static IntervalLattice calculateRange(RangeValueLattice* lattice, const Value* value);

        static void printVariableSet(set<Variable*>* variable, string title);

        static void printVariableTypes(map<VariableKind, int> types, string title);

        private:
        
        static void printVariableMap(map<const Value*, Variable*>* map, string title);

    };
}

#endif
