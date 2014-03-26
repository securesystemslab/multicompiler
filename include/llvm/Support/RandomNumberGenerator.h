//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines Random Number Generator abstraction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/Support/raw_ostream.h"
#include <string>
#include <random>

namespace llvm {

class StringRef;

class RandomNumberGenerator {
public:
  /// Initialized by the frontend using SetSalt. Should contain
  /// unique, deterministic data. Currently initialized to
  /// command-line paramater string, without any randomly generated
  /// arguments.
  static std::string SaltData;

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
  uint64_t Random(uint64_t Max);

private:
  std::default_random_engine generator;

  RandomNumberGenerator();

  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &other) = delete;
  RandomNumberGenerator &
  operator=(const RandomNumberGenerator &other) = delete;

  void Seed(StringRef Salt, uint64_t Seed, uint32_t InstanceID);
};
}

#endif
