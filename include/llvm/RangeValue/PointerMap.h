#ifndef VARIABLEMAP__H
#define VARIABLEMAP__H

#include "DebugSettings.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include <map>
#include <set>

using namespace llvm;
using namespace std;

namespace llvm {

    typedef map<const Value*, set<const Value*> > ValueSets;

    class PointerMap {

        ValueSets pointingToMap;
        ValueSets pointedFromMap;

        bool blackHoleNodes;

        public:

        PointerMap(Module*);

        set<const Value*> getValuesPointedFrom(const Value* value);

        set<const Value*> getValuesPointingTo(const Value* value);

        bool containsBlackHoleNodes();

    };
}
#endif
