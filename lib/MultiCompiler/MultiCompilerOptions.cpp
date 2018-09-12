/*===--- MultiCompilerOptions.cpp - Multicompiler Options -----
*
*
*  This file implements the Multi Compiler Options interface.
*
*  Author: kmanivan
*  Date: Dec 17, 2010
*
*===----------------------------------------------------------------------===*/

#include "llvm/MultiCompiler/MultiCompilerOptions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include "llvm/ADT/SmallVector.h"
#include <fstream>
#include <string>

using namespace llvm;


namespace multicompiler {


llvm::cl::opt<unsigned int>
MaxStackElementPadding("max-stack-element-pad-size",
                        llvm::cl::desc("Maximum amount of stack element padding"),
                        llvm::cl::init(0));

llvm::cl::opt<bool>
StackToHeapPromotion("stack-to-heap-promotion",
                  llvm::cl::desc("Randomly promote stack-allocated buffers to heap allocation"),
                  llvm::cl::init(false));

llvm::cl::opt<unsigned int>
StackToHeapPercentage("stack-to-heap-percentage",
                        llvm::cl::desc("Percentage of stack-to-heap promotion"),
                        llvm::cl::init(30));

llvm::cl::opt<unsigned int>
StackElementPaddingPercentage("stack-element-percentage",
                        llvm::cl::desc("Percentage of padding prepended before stack elements"),
                        llvm::cl::init(0));


llvm::cl::opt<bool>
ShuffleStackFrames("shuffle-stack-frames",
                     llvm::cl::desc("Shuffle variables in function stack frames"),
                     llvm::cl::init(false));

llvm::cl::opt<bool>
ReverseStackFrames("reverse-stack-frames",
                     llvm::cl::desc("Reverse variable layout in function stack frames"),
                     llvm::cl::init(false));

llvm::cl::opt<unsigned int>
MaxStackFramePadding("max-stack-pad-size",
                        llvm::cl::desc("Maximum amount of stack frame padding"),
                        llvm::cl::init(0));

llvm::cl::opt<unsigned int>
GlobalPaddingPercentage("global-padding-percentage",
                        llvm::cl::desc("Percentage of globals that get random padding"),
                        llvm::cl::init(0));

llvm::cl::opt<unsigned int>
GlobalPaddingMaxSize("global-padding-max-size",
                     llvm::cl::desc("Maximum size of random padding between globals, in bytes"),
                     llvm::cl::init(64));

llvm::cl::opt<unsigned int>
GlobalMinCount("global-min-count",
               llvm::cl::desc("Ensure at least N globals in each independently shuffled globals list"),
               llvm::cl::init(0));

llvm::cl::opt<bool>
ShuffleGlobals("shuffle-globals",
               llvm::cl::desc("Shuffle the layout of global variables"),
               llvm::cl::init(false));

llvm::cl::opt<bool>
ReverseGlobals("reverse-globals",
               llvm::cl::desc("Reverse the layout of global variables"),
               llvm::cl::init(false));

llvm::cl::opt<int>
PreRARandomizerRange("pre-RA-randomizer-range",
                        llvm::cl::desc("Pre-RA instruction randomizer probability range; -1 for shuffle"),
                        llvm::cl::init(0));

llvm::cl::opt<unsigned int>
NOPInsertionPercentage("nop-insertion-percentage",
                          llvm::cl::desc("Percentage of instructions that have NOPs prepended"),
                          llvm::cl::init(50));

llvm::cl::opt<unsigned int>
MaxNOPsPerInstruction("max-nops-per-instruction",
                          llvm::cl::desc("Maximum number of NOPs per instruction"),
                          llvm::cl::init(1));

llvm::cl::opt<unsigned int>
EarlyNOPThreshold("early-nop-threshold",
                     llvm::cl::desc("Threshold of inserted NOPs for NOP insertion early-mode"),
                     llvm::cl::init(0));

llvm::cl::opt<unsigned int>
EarlyNOPMaxCount("early-nop-max-count",
                    llvm::cl::desc("Maximum number of NOPs per instruction in NOP early-mode"),
                    llvm::cl::init(5));

llvm::cl::opt<unsigned int>
MOVToLEAPercentage("mov-to-lea-percentage",
                      llvm::cl::desc("Percentage of MOVs that get changed to LEA"),
                      llvm::cl::init(0));

llvm::cl::opt<unsigned int>
EquivSubstPercentage("equiv-subst-percentage",
                      llvm::cl::desc("Percentage of instructions which get equivalent-substituted"),
                      llvm::cl::init(0));

llvm::cl::opt<bool>
RandomizeFunctionList("randomize-function-list",
                       llvm::cl::desc("Permute the function list"),
                       llvm::cl::init(false));

llvm::cl::opt<unsigned int>
FunctionAlignment("align-functions",
                     llvm::cl::desc("Specify alignment of functions as log2(align)"),
                     llvm::cl::init(4));
 
llvm::cl::opt<bool>
RandomizePhysRegs("randomize-machine-registers",
                  llvm::cl::desc("Randomize the order of machine registers used in allocation"),
                  llvm::cl::init(false));
llvm::cl::opt<unsigned int>
ProfiledNOPInsertion("profiled-nop-insertion",
                        llvm::cl::desc("Use profile information in NOP insertion"),
                        llvm::cl::init(0));

llvm::cl::opt<unsigned int>
NOPInsertionRange("nop-insertion-range",
                      llvm::cl::desc("Range of values for NOP insertion percentage"),
                      llvm::cl::init(0));

llvm::cl::opt<bool>
NOPInsertionUseLog("nop-insertion-use-log",
                      llvm::cl::desc("Use a logarithm for NOP insertion"),
                      llvm::cl::init(false));

llvm::cl::opt<unsigned int>
ProfiledNOPMinThreshold("profiled-nop-min-threshold",
                           llvm::cl::desc("Threshold percentage of execution count"
                                          " for minimal NOP insertion"),
                           llvm::cl::init(0));

llvm::cl::opt<bool>
UseFunctionOptions("use-function-options",
                      llvm::cl::desc("Use per-function options"),
                      llvm::cl::init(false));

llvm::cl::opt<std::string>
FunctionOptionsFile("function-options-file",
                       llvm::cl::desc("File to read per-function options from"),
                       llvm::cl::init("function-options.txt"));

// FIXME: read&store this with a LLVM Pass
typedef StringMap<StringMap<std::string> > FunctionOptionMap;
static FunctionOptionMap *funcOptMap = NULL;


void readFunctionOptions() {
  if (funcOptMap != NULL)
    return;

  assert(UseFunctionOptions && "Trying to read function options when disabled");
  funcOptMap = new FunctionOptionMap();
  std::ifstream file(FunctionOptionsFile.c_str());
  if (!file.is_open()) {
    llvm::errs() << "Error: couldn't open per-function options file\n";
    return;
  }

  llvm::Regex funcRE("([_a-zA-Z0-9]+)[ \\t\\n\\f\\r]*{");
  llvm::Regex optRE("([-a-zA-Z]+)=([0-9]+)");
  llvm::Regex endRE("}");
  std::string line;
  llvm::SmallVector<llvm::StringRef, 4> matches;
  while (!file.eof()) {
    getline(file, line);
    if (funcRE.match(line, &matches)) {
      std::string funcName = matches[1];
      for (;;) {
        if (file.eof()) {
          llvm::errs() << "Error: function options reached end of file\n";
          break;
        }
        getline(file, line);
        if (endRE.match(line)) {
          break;
        }
        if (optRE.match(line)) {
          // FIXME: regex doesn't work correctly
          size_t eqPos = line.find_first_of('=');
          std::string optName = line.substr(0, eqPos);
          std::string optVal  = line.substr(eqPos + 1, line.length() - eqPos - 1);
          // FIXME: check if option is valid
          (*funcOptMap)[funcName][optName] = optVal;
        }
        // Ignore everything else
      }
    }
  }
  file.close();
}

bool findFunctionOption(const Function &Fn, StringRef OptName, StringRef &Val) {
  readFunctionOptions();
  FunctionOptionMap::const_iterator FnI = funcOptMap->find(Fn.getName());
  if (FnI == funcOptMap->end())
    return false;
  StringMap<std::string>::const_iterator OptI = FnI->second.find(OptName);
  if (OptI == FnI->second.end())
    return false;

  Val = OptI->second;
  return true;
}

}
