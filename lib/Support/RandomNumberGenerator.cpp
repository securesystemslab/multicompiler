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
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Atomic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/raw_ostream.h"

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

/// This RNG is an implementation of the standard NIST SP 800-90A
/// HMAC_DRBG random number generator, however with MD5 as the hash
/// function. The use of MD5 does not comply with the NIST standard,
/// which specifies SHA as the hash function instead. Since we are
/// using HMAC-MD5, instead of plain MD5, and the random number stream
/// is not directly revealed to an attacker, this should be
/// sufficient.
///
/// Since this RNG is initialized from a 64-bit seed, it has only 64
/// bits of entropy, rather than 128 as might be expected from
/// MD5. This should not be a problem as long as possible attacks
/// cannot brute-force through 2^64 possibilities.
///
/// Note: We do NOT perform reseeding due to constrained entropy
/// data. Since we need reproducibility, we can only use the given
/// 64-bit seed as entropy, and therefore have no additional entropy
/// to reseed with. This means that we assume less than 2^48 calls to
/// Random(). However, it is unlikely that compromise of the RNG is
/// even possible in practice since the actual random stream is not
/// exposed to an attacker.
RandomNumberGenerator::RandomNumberGenerator() {
  // Make sure each thread is seeded with a different seed
  unsigned InstanceID = sys::AtomicIncrement(&InstanceCount);

  if (RandomSeed == 0 && SaltData.empty())
    DEBUG(errs() << "Warning! Using unseeded random number generator\n");

  Seed(SaltData, RandomSeed + InstanceID);
}

void RandomNumberGenerator::HMAC(BlockType Key, ArrayRef<uint8_t> Text,
                                 BlockType &Result) {
  uint8_t Buffer[64];

  // Set up ipad
  memset(Buffer, 0, sizeof(Buffer));
  memcpy(Buffer, Key, 16);
  for (unsigned i = 0; i < 64; ++i) {
    Buffer[i] ^= 0x36;
  }

  MD5 InnerHash;
  MD5::MD5Result InnerResult;
  InnerHash.update(makeArrayRef(Buffer));
  InnerHash.update(Text);
  InnerHash.final(InnerResult);

  // Set up opad
  memset(Buffer, 0, sizeof(Buffer));
  memcpy(Buffer, Key, 16);
  for (unsigned i = 0; i < 64; ++i) {
    Buffer[i] ^= 0x5c;
  }

  MD5 OuterHash;
  OuterHash.update(makeArrayRef(Buffer));
  OuterHash.update(makeArrayRef(InnerResult));
  OuterHash.final(Result);
}

void RandomNumberGenerator::HMAC_DRBG_Update(ArrayRef<uint8_t> Data =
                                                 ArrayRef<uint8_t>()) {
  SmallVector<uint8_t, 17> Buffer(Key, Key + 16);
  Buffer.push_back(0x00);
  Buffer.append(Data.begin(), Data.end());
  HMAC(Key, Buffer, Key);
  HMAC(Key, Value, Key);
  if (Data.size() == 0)
    return;

  Buffer.clear();
  Buffer.append(Key, Key + 16);
  Buffer.push_back(0x01);
  Buffer.append(Data.begin(), Data.end());
  HMAC(Key, Buffer, Key);
  HMAC(Key, Value, Value);
}

void RandomNumberGenerator::Seed(StringRef Salt, uint64_t Seed) {
  DEBUG(dbgs() << "Re-Seeding RNG from salt and seed\n");
  DEBUG(dbgs() << "Salt: " << Salt << "\n");
  DEBUG(dbgs() << "Seed: " << Seed << "\n");

  memset(Key, 0, sizeof(Key));
  memset(Value, 1, sizeof(Value));

  unsigned SeedSize = sizeof(Seed) + Salt.size();
  uint8_t *SeedMaterial = new uint8_t[SeedSize];
  memcpy(SeedMaterial, &Seed, sizeof(Seed));
  memcpy(SeedMaterial + sizeof(Seed), Salt.data(), Salt.size());

  HMAC_DRBG_Update(makeArrayRef(SeedMaterial, SeedSize));

  delete[] SeedMaterial;
}

uint64_t RandomNumberGenerator::Random() {
  HMAC(Key, Value, Value);
  uint64_t Output = *(uint64_t *)(Value);

  HMAC_DRBG_Update();

  return Output;
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
