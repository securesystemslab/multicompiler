#ifndef LLVM_DATARANOD_PASSES_H
#define LLVM_DATARANOD_PASSES_H

namespace llvm {

class ModulePass;
ModulePass *createDataChecksPass();

// HeapChecks - This pass instruments the code to perform run-time cross-checking
// of what objects are accessed by loads, stores, and other memory operations.
ModulePass *createHeapChecksPass();

}

#endif
