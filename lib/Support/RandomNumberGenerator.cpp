//===-- RandomNumberGenerator.cpp - Implement RNG class -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements deterministic random number generation (RNG).
// The current implementation is NOT cryptographically secure as it uses
// the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "rng"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RandomNumberGenerator.h"

using namespace llvm;

// Tracking BUG: 19665
// http://llvm.org/bugs/show_bug.cgi?id=19665
//
// Do not change to cl::opt<uint64_t> since this silently breaks argument parsing.
static cl::opt<unsigned long long>
Seed("rng-seed", cl::value_desc("seed"),
     cl::desc("Seed for the random number generator"), cl::init(0));

RandomNumberGenerator::RandomNumberGenerator(const Module &M, StringRef PassSalt) {
  DEBUG(
    if (Seed == 0)
      dbgs() << "Warning! Using unseeded random number generator.\n"
  );

  // This RNG is guaranteed to produce the same random stream only
  // when the Module ID and thus the input filename is the same. This
  // might be problematic if the input filename extension changes
  // (e.g. from .c to .bc or .ll).
  //
  // We could store this salt in NamedMetadata, but this would make
  // the parameter non-const. This would unfortunately make this
  // interface unusable by any Machine passes, since they only have a
  // const reference to their IR Module. Alternatively we can always
  // store salt metadata from the Module constructor.
  StringRef ModuleSalt = sys::path::filename(M.getModuleIdentifier());

  // Combine seed and salts using std::seed_seq.
  // Data: Seed-low, Seed-high, ModuleSalt, PassSalt
  // Note: std::seed_seq can only store 32-bit values, even though we
  // are using a 64-bit RNG. This isn't a problem since the Mersenn
  // twister constructor copies these correctly into its initial state
  std::vector<uint32_t> Data;
  Data.reserve(2 + ModuleSalt.size() + PassSalt.size());
  Data.push_back(Seed);
  Data.push_back(Seed >> 32);

  std::vector<uint32_t>::iterator I = Data.end();
  I = std::copy(ModuleSalt.begin(), ModuleSalt.end(), I);
  I = std::copy(PassSalt.begin(), PassSalt.end(), I);

  std::seed_seq SeedSeq(Data.begin(), Data.end());
  Generator.seed(SeedSeq);
}

uint_fast64_t RandomNumberGenerator::operator()() {
  return Generator();
}
