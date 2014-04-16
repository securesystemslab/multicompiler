//===-- RandomNumberGenerator.cpp - Implement RNG class -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements random number generation (RNG).
// The current implementation is NOT cryptographically secure as it uses
// the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "rng"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ThreadLocal.h"

using namespace llvm;

// Initialized once, then only read by threads, so no locking required.
static std::string SaltData;

// Do not change to cl::opt<uint64_t> since this silently breaks argument parsing.
static cl::opt<unsigned long long>
RandomSeed("rng-seed", cl::value_desc("seed"),
           cl::desc("Seed for the random number generator"), cl::init(0));

static cl::opt<std::string, true>
SaltDataOpt("entropy-data",
            cl::desc("Entropy data for the RNG (testing only, should be set "
                     "by command line options"),
            cl::Hidden, cl::location(SaltData));

static ManagedStatic<sys::ThreadLocal<const RandomNumberGenerator> > Instance;

void RandomNumberGenerator::SetSalt(const StringRef &Salt) {
  SaltData = Salt;
}

RandomNumberGenerator *RandomNumberGenerator::Get() {
  RandomNumberGenerator *RNG =
      const_cast<RandomNumberGenerator *>(Instance->get());

  if (RNG == 0) {
    RNG = new RandomNumberGenerator;
    Instance->set(RNG);
  }

  return RNG;
}

// Note that every new RNG will produce the same stream of
// pseudo-random numbers, unless SetSalt is called again.
RandomNumberGenerator::RandomNumberGenerator() {
  if (RandomSeed == 0 && SaltData.empty())
    DEBUG(errs()
          << "Warning! Using unseeded and unsalted random number generator\n");

  Seed(SaltData, RandomSeed);
}

uint64_t RandomNumberGenerator::Random(uint64_t Max) {
  std::uniform_int_distribution<uint64_t> distribution(0, Max - 1);
  return distribution(generator);
}

void RandomNumberGenerator::Seed(StringRef Salt, uint64_t Seed) {
  DEBUG(dbgs() << "Re-Seeding RNG from salt and seed\n");
  DEBUG(dbgs() << "Salt: " << Salt << "\n");
  DEBUG(dbgs() << "Seed: " << Seed << "\n");

  // Sequence: Seed-low, Seed-high, Salt...
  unsigned SeedSize = Salt.size() + 2;
  unsigned Seeds[SeedSize];
  Seeds[0] = Seed;
  Seeds[1] = Seed >> 32;
  for (unsigned i = 0; i < Salt.size(); ++i)
    Seeds[2 + i] = Salt[i];

  std::seed_seq SeedSeq(Seeds, Seeds + SeedSize);
  generator.seed(SeedSeq);
}
