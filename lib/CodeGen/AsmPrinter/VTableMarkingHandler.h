//===-- llvm/lib/CodeGen/AsmPrinter/VTableMarkingHandler.h ----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing line tables info into COFF files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_VTABLEMARKINGHANDLER_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_VTABLEMARKINGHANDLER_H

#include "AsmPrinterHandler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

namespace llvm {
/// \brief Collects and handles line tables information in a CodeView format.
class VTableMarkingHandler : public AsmPrinterHandler {
  AsmPrinter *Asm;

  // Store the functions we've visited in a vector so we can maintain a stable
  // order while emitting subsections.
  SmallVector<const Function *, 10> VisitedFunctions;

  const MachineFunction *MF;

  struct VCall {
    MCSymbol *VCallSym;
    const GlobalVariable *Name;
    bool isMethodPointer;

    VCall(MCSymbol *S, const GlobalVariable *N, bool MP) : VCallSym(S), Name(N), isMethodPointer(MP) {}
  };

  /// All vcalls contained in the regular .text section, not uniqued .text.*
  /// sections
  std::vector<VCall> TextCalls;

  /// Current list of vcalls, appended into TextCalls or emitted at the end of
  /// each function
  std::vector<VCall> Calls;

  void EmitHeader(bool emitVTables, bool emitVCalls);
  void EmitVCalls(std::vector<VCall> &Calls);
  void EmitVTables(const ConstantArray *VTables);

public:
  VTableMarkingHandler(AsmPrinter *AP)
    : Asm(AP) {}

  void setSymbolSize(const MCSymbol *, uint64_t) override {}

  /// \brief Emit the COFF section that holds the line table information.
  void endModule() override;

  /// \brief Gather pre-function debug information.
  void beginFunction(const MachineFunction *MF) override;

  /// \brief Gather post-function debug information.
  void endFunction(const MachineFunction *) override;

  /// \brief Process beginning of an instruction.
  void beginInstruction(const MachineInstr *MI) override;

  /// \brief Process end of an instruction.
  void endInstruction() override {}
};
} // End of namespace llvm

#endif
