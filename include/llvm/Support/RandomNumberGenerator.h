//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for random number generation (RNG).
// Note that the current implementation is not cryptographically secure
// as it uses the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/ADT/StringRef.h"
#include <string>
#include <random>

namespace llvm {

class RandomNumberGenerator {
public:
  /// \brief Add additional personalization data to the RNG seed.
  ///
  /// This function should be used to add deterministic command line
  /// argument data to the RNG initialization, resulting in a
  /// different stream of random numbers for each invocation during a
  /// build. The input to this function should be unique per
  /// compilation unit.
  static void SetSalt(const StringRef &Salt);

  static RandomNumberGenerator *Get();

  /// \brief Returns a random number in the range [0, Max).
  uint64_t Random(uint64_t Max);

private:
  std::default_random_engine generator;

  void Seed(StringRef Salt, uint64_t Seed, unsigned InstanceID);

  RandomNumberGenerator();
  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &other) = delete;
  RandomNumberGenerator &
  operator=(const RandomNumberGenerator &other) = delete;
};
}

#endif
