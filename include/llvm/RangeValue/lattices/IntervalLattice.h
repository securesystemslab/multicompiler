#ifndef INTERVALLATTICE_H
#define INTERVALLATTICE_H

#include "../DebugSettings.h"
#include <string>

using namespace std;

namespace llvm {
    // The minimum value represented by the interval lattices (this or any lower values represent minus infinity)
    const int INTERVAL_MIN = -2;
    
    // The maximum value represented by the interval lattices (this or any higher values represent plus infinity)
    const int INTERVAL_MAX = 2;

    #define ARITHMETIC_RESULT(l, h) IntervalLattice( (this->low == INTERVAL_MIN || other->low == INTERVAL_MIN) ? INTERVAL_MIN : (l),\
                                                    (this->high == INTERVAL_MAX || other->high == INTERVAL_MAX) ? INTERVAL_MAX : (h),\
                                                    this->precisionStatus | other->precisionStatus) 

    /*
     * The lattice for an interval for a single variable
     * */
    class IntervalLattice{
        public:

            enum PrecisionStatus {
                PS_Defined = 0,
                PS_LostArithmetic = 1,
                PS_LostConstant = 2,
                PS_LostUnknownValue = 4,
                PS_LostUnknownConstant = 8,
                PS_LostArrayField = 16,
                PS_NeverPreciselyAssigned = 32,
                PS_LostUnknownElementIndex = 64,
                PS_LostPointedTo = 128,
                PS_LostPointerSelfReference = 256
            };
        private:    
            int low, high;

            PrecisionStatus precisionStatus;
        public:
            IntervalLattice();
            IntervalLattice(long low, long high, PrecisionStatus precisionStatus);
            IntervalLattice(IntervalLattice* lattice);

            bool leq(IntervalLattice* lattice) const;

            IntervalLattice join(IntervalLattice lattice) const;

            string toStr() const;

            bool isBottom() const;

            bool isTop() const;

            bool isPartiallyOpen() const;

            int getHigh() const;

            int getLow() const;

            IntervalLattice add(IntervalLattice* other) const;

            IntervalLattice subtract(IntervalLattice* other) const;

            IntervalLattice multiply(IntervalLattice* other) const;

            IntervalLattice divide(IntervalLattice* other) const;

            PrecisionStatus getPrecisionStatus();

            void addImprecisionReason(PrecisionStatus reason);

            static IntervalLattice top(PrecisionStatus status);

            static IntervalLattice bottom();	
    };
    
    inline IntervalLattice::PrecisionStatus operator|(IntervalLattice::PrecisionStatus a, IntervalLattice::PrecisionStatus b) {
        return static_cast<IntervalLattice::PrecisionStatus>(static_cast<int>(a) | static_cast<int>(b)); 
    }
}

#endif
