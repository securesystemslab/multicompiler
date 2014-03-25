//===-- RandomNumberGenerator.cpp - Implement RNG class -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements cryptographically secure random number generation
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "rng"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/Atomic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ThreadLocal.h"

using namespace llvm;

// Only read by threads, so no locking
// Needs to be available for SaltDataOpt to set by testing
// command-line, so must be public
std::string RandomNumberGenerator::SaltData;

static cl::opt<unsigned long long>
RandomSeed("rng-seed", cl::value_desc("seed"),
           cl::desc("Seed for the random number generator"), cl::init(0));

static cl::opt<std::string, true>
SaltDataOpt("entropy-data",
            cl::desc("Entropy data for the RNG (testing only, should be set "
                     "by command line options"),
            cl::Hidden, cl::location(RandomNumberGenerator::SaltData));

static ManagedStatic<sys::ThreadLocal<const RandomNumberGenerator> > Instance;
static unsigned InstanceCount = 0;

RandomNumberGenerator::RandomNumberGenerator() {
  // Make sure each thread is seeded with a different seed
  unsigned InstanceID = sys::AtomicIncrement(&InstanceCount);

  if (RandomSeed == 0 && SaltData.empty())
    DEBUG(errs() << "Warning! Using unseeded random number generator\n");

  Seed(SaltData, RandomSeed + InstanceID);
}

void RandomNumberGenerator::Seed(StringRef Salt, uint64_t Seed) {
  DEBUG(dbgs() << "Re-Seeding RNG from salt and seed\n");
  DEBUG(dbgs() << "Salt: " << Salt << "\n");
  DEBUG(dbgs() << "Seed: " << Seed << "\n");

  generator.seed(Seed);
  // TODO: How to incorporate Salt into seed?
}

uint64_t RandomNumberGenerator::Random(uint64_t Max) {
  std::uniform_int_distribution<int> distribution(0, Max - 1);
  return distribution(generator);
}

RandomNumberGenerator *RandomNumberGenerator::Generator() {
  RandomNumberGenerator *RNG =
      const_cast<RandomNumberGenerator *>(Instance->get());

  if (RNG == 0) {
    RNG = new RandomNumberGenerator;
    Instance->set(RNG);
  }

  return RNG;
}
