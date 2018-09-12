
#ifndef WORKLIST_H
#define WORKLIST_H

#include "DebugSettings.h"

#include "RangeValueConstraint.h"
#include <string>

namespace llvm {

    struct rvc_compare {
        bool operator()(const RangeValueConstraint* x, const RangeValueConstraint* y) const {
            return RangeValueConstraint::compare(x, y);
        }
    };
    /*
     * The abstact worklist definition
     * */
    class Worklist{

        public:
            virtual void clear() = 0;

            virtual void insert(RangeValueConstraint* constraint) = 0;

            virtual RangeValueConstraint* extract() = 0;

            virtual bool isEmpty() = 0;

            virtual string toStr() = 0;

            virtual ~Worklist(){}
    };

}

#endif
