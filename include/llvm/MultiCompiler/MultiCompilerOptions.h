//==- MultiCompilerOptions.h - Multicompiler Options -------------*- C++ -*-==//
//===----------------------------------------------------------------------===//
//
//
//
//  This file defines the Multi Compiler Options interface.
//
//===----------------------------------------------------------------------===//

#ifndef MULTI_COMPILER_OPTIONS_H
#define MULTI_COMPILER_OPTIONS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <stdint.h>
#include <string>

namespace multicompiler {

using namespace llvm;

extern cl::opt<unsigned int> MaxStackElementPadding;
extern cl::opt<bool> StackToHeapPromotion;
extern cl::opt<unsigned int> StackToHeapPercentage;
extern cl::opt<unsigned int> StackElementPaddingPercentage;
extern cl::opt<bool> ShuffleStackFrames;
extern cl::opt<bool> ReverseStackFrames;
extern cl::opt<unsigned int> MaxStackFramePadding;
extern cl::opt<std::string> MultiCompilerSeed;
extern cl::opt<int> PreRARandomizerRange;
extern cl::opt<std::string> RNGStateFile;
extern cl::opt<unsigned int> NOPInsertionPercentage;
extern cl::opt<unsigned int> MaxNOPsPerInstruction;
extern cl::opt<unsigned int> EarlyNOPThreshold;
extern cl::opt<unsigned int> EarlyNOPMaxCount;
extern cl::opt<unsigned int> MOVToLEAPercentage;
extern cl::opt<unsigned int> EquivSubstPercentage;
extern cl::opt<bool> RandomizeFunctionList;
extern cl::opt<unsigned int> FunctionAlignment;
extern cl::opt<bool> RandomizePhysRegs;
extern cl::opt<unsigned int> ISchedRandPercentage;
extern cl::opt<unsigned int> ProfiledNOPInsertion;
extern cl::opt<unsigned int> NOPInsertionRange;
extern cl::opt<bool> NOPInsertionUseLog;
extern cl::opt<unsigned int> ProfiledNOPMinThreshold;
extern cl::opt<bool> UseFunctionOptions;
extern cl::opt<std::string> FunctionOptionsFile;
extern cl::opt<unsigned int> GlobalPaddingPercentage;
extern cl::opt<unsigned int> GlobalPaddingMaxSize;
extern cl::opt<unsigned int> GlobalMinCount;
extern cl::opt<bool> ShuffleGlobals;
extern cl::opt<bool> ReverseGlobals;

static const int NOPInsertionUnknown = -1;

bool findFunctionOption(const Function &Fn, StringRef OptName, StringRef &Val);

void readFunctionOptions();

template<class DataType, bool ExternalStorage, class ParserClass>
DataType getFunctionOption(cl::opt<DataType, ExternalStorage, ParserClass> &O, const llvm::Function &Fn) {
  if (!UseFunctionOptions)
    return O;

  StringRef ValStr;
  DataType Val;
  if (findFunctionOption(Fn, O.ArgStr, ValStr)) {
    if (!O.getParser().parse(O, O.ArgStr, ValStr, Val))
      return Val;
    else
      llvm::errs() << "Error: couldn't parse option for " << Fn.getName()
                   << "::" << O.ArgStr << ", reverting to global value\n";
  }
  return O;
}

}

#endif
