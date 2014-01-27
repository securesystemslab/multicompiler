//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the crypto-secure Random Number Generator. This
// RNG is based on HMAC_DRBG with MD5 as the hash.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/Config/config.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/ThreadLocal.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <inttypes.h>

namespace llvm {

class StringRef;

#define BLOCK_SIZE 16 // MD5 has a block size of 128 bits

/// RandomNumberGenerator is a crypto-secure RNG which can be used to
/// generate randomness for security uses. The RNG is based on the
/// standard HMAC_DRBG with MD5 as the hash function.
class RandomNumberGenerator {
public:
  /// Initialized by the frontend using SetSalt. Should contain
  /// unique, deterministic data. Currently initialized to
  /// command-line paramater string, without any randomly generated
  /// arguments.
  static std::string SaltData;

  uint64_t Random();

  static RandomNumberGenerator *Generator();

  /// \brief Add additional personalization data to the RNG seed
  ///
  /// This function should be used to add deterministic command line
  /// argument data to the RNG initialization, resulting in a
  /// different stream of random numbers for each invocation during a
  /// build. The input to this function should be unique per
  /// compilation unit.
  static void SetSalt(const StringRef &Salt) { SaltData = Salt; }

  /// \brief Returns a random number in the range [0, Max)
  ///
  /// Uses sampling to make sure that the result is not biased because
  /// Max does not divide evenly into 2^64
  uint64_t Random(uint64_t Max) {
    uint64_t t = Max * (((uint64_t)1 << 63) / Max);
    uint64_t r;
    while ((r = Random()) >= t)
      ; /*noop */

    return r % Max;
  }

private:
  typedef uint8_t BlockType[BLOCK_SIZE];

  BlockType Value;
  BlockType Key;

  RandomNumberGenerator();

  // Noncopyable.
  RandomNumberGenerator(
      const RandomNumberGenerator &other) LLVM_DELETED_FUNCTION;
  RandomNumberGenerator &
  operator=(const RandomNumberGenerator &other) LLVM_DELETED_FUNCTION;

  void HMAC(BlockType Key, ArrayRef<uint8_t> Text, BlockType &Result);
  void HMAC_DRBG_Update(ArrayRef<uint8_t> Data);

  void Seed(StringRef Salt, uint64_t Seed);
};
}

#endif
