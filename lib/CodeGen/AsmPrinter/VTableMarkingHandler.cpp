//===-- llvm/lib/CodeGen/AsmPrinter/VTableMarkingHandler.cpp --*- C++ -*--===//
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

#include "VTableMarkingHandler.h"
#include "llvm/IR/Instructions.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "vtable-marking"

namespace {
enum TexTrapFlags {
  hasFunctionStarts = 1 << 8,
  hasSortedSections = 1 << 9,
  hasSymbolSizes = 1 << 10,
  hasDataRefs = 1 << 11,
  hasVTables = 1 << 12,
  hasVCalls = 1 << 13,
};
}

static void EmitLabelDiff(MCStreamer &Streamer,
                          const MCSymbol *From, const MCSymbol *To) {
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  MCContext &Context = Streamer.getContext();
  const MCExpr *FromRef = MCSymbolRefExpr::create(From, Variant, Context),
               *ToRef   = MCSymbolRefExpr::create(To, Variant, Context);
  const MCExpr *AddrDelta =
      MCBinaryExpr::create(MCBinaryExpr::Sub, ToRef, FromRef, Context);
  Streamer.EmitULEB128Value(AddrDelta);
}

void VTableMarkingHandler::EmitHeader(bool emitVTables, bool emitVCalls) {
  if (!emitVTables && !emitVCalls)
    return;

  uint32_t Flags = 2; // version
  if (emitVTables)
    Flags |= hasVTables;
  if (emitVCalls)
    Flags |= hasVCalls;
  Asm->OutStreamer->EmitIntValue((uint32_t)Flags, 4);
}

void VTableMarkingHandler::EmitVCalls(std::vector<VCall> &Calls) {
  if (Calls.empty())
    return;

  DEBUG(dbgs() << "Adding vcalls to textrap\n");
  unsigned PtrSize = Asm->getDataLayout().getPointerSize(0);

  MCSymbol *FirstVCSym = Calls[0].VCallSym;
  Asm->EmitLabelReference(FirstVCSym, PtrSize);
  for (auto Call : Calls) {
    DEBUG(dbgs() << "Emitting vcall at ");
    DEBUG(Call.VCallSym->dump());
    DEBUG(dbgs() << " to ");
    DEBUG(Call.Name->dump());
    DEBUG(if (Call.isMethodPointer) dbgs() << " (method)");
    DEBUG(dbgs() << '\n');
    EmitLabelDiff(*Asm->OutStreamer, FirstVCSym, Call.VCallSym);
    Asm->EmitLabelReference(Asm->getSymbol(Call.Name), PtrSize);
    Asm->OutStreamer->EmitIntValue(Call.isMethodPointer, 1); // mark whether this call is shifted
    // Asm->OutStreamer->EmitULEB128IntValue(ClassMap[Call.Name]);
  }
  Asm->OutStreamer->EmitIntValue(0, 1); // end of vcalls
  Asm->OutStreamer->EmitIntValue(0, PtrSize); // end of vcalls
}

void VTableMarkingHandler::EmitVTables(const ConstantArray *VTables) {
  if (!VTables)
    return;

  unsigned PtrSize = Asm->getDataLayout().getPointerSize(0);

  for (auto &VTInfoOp : VTables->operands()) {
    const User *VTInfo = cast<User>(VTInfoOp);
    const Constant *VTable = cast<Constant>(VTInfo->getOperand(0));

    MCSection *TexTrapSection = Asm->getObjFileLowering().SectionForGlobal(
      cast<GlobalValue>(VTable->stripInBoundsConstantOffsets()),
      SectionKind::getTexTrap(false), *Asm->Mang, Asm->TM);

    bool UniqueSection = TexTrapSection != Asm->getObjFileLowering().getTexTrapSection();
    if (UniqueSection) {
      Asm->OutStreamer->PushSection();
      Asm->OutStreamer->SwitchSection(TexTrapSection);

      EmitHeader(true, false);
    }

    DEBUG(dbgs() << "Emitting vtable ");
    DEBUG(VTable->dump());

    Asm->OutStreamer->EmitValue(Asm->lowerConstant(VTable), PtrSize);

    ConstantInt *NumMethods = cast<ConstantInt>(VTInfo->getOperand(1));
    Asm->OutStreamer->EmitULEB128IntValue(NumMethods->getZExtValue());

    const GlobalVariable *BaseNamesVar = cast<GlobalVariable>(VTInfo->getOperand(2)->stripInBoundsConstantOffsets());
    for (auto &BaseName : BaseNamesVar->getInitializer()->operands()) {
      DEBUG(dbgs() << "Adding base ");
      DEBUG(BaseName->dump());
      Asm->EmitLabelReference(Asm->getSymbol(cast<GlobalVariable>(BaseName->stripPointerCasts())), PtrSize);
    }
    Asm->OutStreamer->EmitIntValue(0, PtrSize); // end of bases

    if (UniqueSection) {
      Asm->OutStreamer->EmitIntValue(0, PtrSize); // end of vtables
      Asm->OutStreamer->PopSection();
    }
  }

  Asm->OutStreamer->EmitIntValue(0, PtrSize); // end of vtables
}

void VTableMarkingHandler::endModule() {
  DEBUG(dbgs() << "Finishing module\n");

  const ConstantArray *VTables = nullptr;
  for (const auto &G : Asm->MMI->getModule()->globals()) {
    if (G.getName() == "llvm.trap.vtables") {
      VTables = cast<ConstantArray>(G.getInitializer());
      break;
    }
  }

  if (!VTables && TextCalls.empty())
    return;

  Asm->OutStreamer->SwitchSection(
    Asm->getObjFileLowering().getTexTrapSection());

  EmitHeader(VTables,
             !TextCalls.empty());

  EmitVTables(VTables);
  EmitVCalls(TextCalls);
}

void VTableMarkingHandler::beginFunction(const MachineFunction *Fn) {
  MF = Fn;
  DEBUG(dbgs() << "Beginning function " << MF->getName() << '\n');
}

void VTableMarkingHandler::endFunction(const MachineFunction *MF) {
  if (!Calls.empty()) {
    MCSection *TexTrapSection = Asm->getObjFileLowering().SectionForGlobal(
      MF->getFunction(), SectionKind::getTexTrap(true), *Asm->Mang, Asm->TM);

    if (TexTrapSection != Asm->getObjFileLowering().getTexTrapSection()) {
      Asm->OutStreamer->PushSection();
      Asm->OutStreamer->SwitchSection(TexTrapSection);

      EmitHeader(false, true);

      EmitVCalls(Calls);

      Asm->OutStreamer->PopSection();
    } else {
      TextCalls.insert(TextCalls.end(), Calls.begin(), Calls.end());
    }

    // Clear calls whether we emitted them here or copied them into TextCalls
    Calls.clear();
  }
}

// For split vtables
void VTableMarkingHandler::beginInstruction(const MachineInstr *MI) {
  if (MI->isPosition() || MI->isImplicitDef() || MI->isKill() ||
      MI->isDebugValue())
    return;

  TrapInfo TI = MI->getTrapInfo();
  if (!TI.isUnknown()) {
    DEBUG(dbgs() << "Found new-style vcall trap info ");
    DEBUG(MI->print(dbgs(), &(Asm->TM)));

    MCSymbol *VCallSym = Asm->OutContext.createTempSymbol();
    Asm->OutStreamer->EmitLabel(VCallSym);

    const GlobalVariable *Name = TI.getClassName();
    if (TI.isMethodPointer())
      DEBUG(dbgs() << "Adding method pointer assignment to ");
    else
      DEBUG(dbgs() << "Adding vcall to ");
    DEBUG(MI->dump());
    DEBUG(dbgs() << " at ");
    DEBUG(VCallSym->dump());
    DEBUG(dbgs() << '\n');
    Calls.push_back(VCall(VCallSym, Name, TI.isMethodPointer()));
    return;
  }
/*
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isTexTrapInfo()) {
      DEBUG(dbgs() << "Found vcall index ");
      DEBUG(MI->print(dbgs(), &(Asm->TM)));
      DEBUG(dbgs() << '\n');

      DEBUG(MO.getTexTrapInfo()->dump());

      MCSymbol *VCallSym = Asm->OutContext.CreateTempSymbol();
      Asm->OutStreamer->EmitLabel(VCallSym);

      ValueAsMetadata *ValueMD = cast<ValueAsMetadata>(MO.getTexTrapInfo()->getOperand(0));
      GlobalVariable *Name = cast<GlobalVariable>(ValueMD->getValue());
      DEBUG(dbgs() << "Adding call to ");
      DEBUG(MI->dump());
      DEBUG(dbgs() << " at ");
      DEBUG(VCallSym->dump());
      DEBUG(dbgs() << '\n');
      Calls.push_back(VCall(VCallSym, Name));
      return;
    }
  }

  for (const auto MMO : MI->memoperands()) {
    if (MMO->getVTableInfo()) {
      DEBUG(dbgs() << "Found MMO with vtableinfo!\n");
      DEBUG(MI->dump());
      return;
    }
  }
*/
}
