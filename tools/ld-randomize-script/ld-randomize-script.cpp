//===-- llc.cpp - Implement the LLVM Native Code Generator ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the llc code generator driver. It provides a convenient
// command-line interface for generating native assembly-language code
// or C code, given LLVM bitcode.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Triple.h"
#include "llvm/Config/config.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include <iostream>
#include <memory>
using namespace llvm;

static const int PAGE_SIZE = 4096;

static cl::opt<uint32_t>
MinBaseAddress("min-base-address", cl::desc("minimum address of program base (inclusive)"),
               cl::init(0x00010000));
static cl::opt<uint32_t>
MaxBaseAddress("max-base-address", cl::desc("maximum address of program base (inclusive)"),
               cl::init(0x09000000));
static cl::opt<uint32_t>
OldBaseAddress("old-base-address", cl::desc("old address of program base"),
               cl::init(0x08048000));


int main(int argc, char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;
  cl::ParseCommandLineOptions(argc, argv, "linker script randomizer\n");

  ErrorOr<std::string> ldPath = llvm::sys::findProgramByName("ld");
  if (ldPath.getError()) {
    errs() << "Couldn't find system linker\n";
    llvm_shutdown();
    return 1;
  }

  SmallString<128> scriptPath;
  std::error_code EC = llvm::sys::fs::createUniqueFile("ld_script-%%%%%%",
                                                       scriptPath);
  if (EC) {
    errs() << "Couldn't open a temporary file\n";
    llvm_shutdown();
    return 1;
  }

  StringRef devNull;
  std::string errMsg;
  const char *ldArgs[] = { ldPath->c_str(), "--verbose", NULL };
  StringRef scriptPathRef = scriptPath;
  const StringRef *redirects[] = { &devNull, &scriptPathRef, &scriptPathRef,
                                   NULL };
  if (sys::ExecuteAndWait(*ldPath, ldArgs, 0, redirects, 0, 0, &errMsg)) {
    errs() << "Error executing linker";
    llvm_shutdown();
    return 1;
  }

  auto scriptFile = MemoryBuffer::getFile(scriptPath.c_str());
  if (!scriptFile) {
    errs() << "Error reading script file\n";
    llvm_shutdown();
    return 1;
  }
 
  uint32_t minPage = (MinBaseAddress + PAGE_SIZE - 1) / PAGE_SIZE,
           maxPage = MaxBaseAddress / PAGE_SIZE;
  if (minPage > maxPage) {
    errs() << "Base address interval is empty";
    llvm_shutdown();
    return 1;
  }
  uint32_t newAddr = PAGE_SIZE * (minPage + 
    RandomNumberGenerator::Generator().Random(maxPage - minPage + 1));

  char oldAddrStr[12];
  sprintf(oldAddrStr, "0x%08x", OldBaseAddress.getValue());

  llvm::Regex oldAddrRegex(oldAddrStr);
  StringRef scriptText = scriptFile->get()->getBuffer();
  StringRef oldScriptText = scriptText;
  char newAddrStr[12];
  sprintf(newAddrStr, "0x%08x", newAddr);
  do {
    // Replace one occurrence of old address with new one
    oldScriptText = scriptText;
    scriptText = oldAddrRegex.sub(newAddrStr, scriptText);
  } while (!scriptText.equals(oldScriptText));
  std::cout << scriptText.str();
  return 0;
}

