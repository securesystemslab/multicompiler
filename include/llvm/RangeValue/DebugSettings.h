
extern bool PrintFlag;
extern bool PrintVarsFlag;
extern bool PrintConstraintsFlag;
extern bool PrintResultsFlag;
extern bool PrintStatsFlag;
extern bool PrintConstraintsEvaluationFlag;

#ifdef NOPRINT
#define PRINT_INFO(X)
#define PRINT_VARIABLE(X)
#define PRINT_CONSTRAINTS(X)
#define PRINT_RESULTS(X)
#define PRINT_CONSTRAINTS_EVAL(X)
#define PRINT_STATS(X)
#else
#define PRINT_IF(X, COND) do { if (PrintFlag || (COND)) {X; } }  while (0)
#define PRINT_INFO(X) PRINT_IF(X, PrintFlag)
#define PRINT_VARIABLE(X) PRINT_IF (X, PrintVarsFlag)
#define PRINT_CONSTRAINTS(X) PRINT_IF (X, PrintConstraintsFlag) 
#define PRINT_RESULTS(X) PRINT_IF (X, PrintResultsFlag)  
#define PRINT_STATS(X) PRINT_IF (X, PrintStatsFlag)  
#define PRINT_CONSTRAINTS_EVAL(X) PRINT_IF (X, PrintConstraintsEvaluationFlag)
#endif

#include "llvm/Support/raw_ostream.h"
