//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for deterministic random number
// generation (RNG).  Note that the current implementation is not
// cryptographically secure as it uses the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h" // Needed for uint64_t on Windows.
#include <random>

namespace llvm {

/// A random number generator.
/// Instances of this class should not be shared across threads.
class RandomNumberGenerator {
public:
  /// Seeds and salts the underlying RNG engine. The seed can be set
  /// on the command line via -rng-seed=<uint64>.
  ///
  /// The RNG is salted with the Module ID of M and a salt (usually
  /// the pass name) provided by the client pass. Each pass which
  /// needs randomness should instantiate its own pass, using a unique
  /// seed. This ensures a reproducible random stream even when other
  /// randomness consuming passes are added or removed.
  RandomNumberGenerator(StringRef Salt);

  /// Returns a random number in the range [0, Max).
  uint_fast64_t operator()();

private:
  // 64-bit Mersenne Twister by Matsumoto and Nishimura, 2000
  // http://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
  // This RNG should be deterministicly portable across C++11
  // implementations.
  std::mt19937_64 Generator;

  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &other)
      LLVM_DELETED_FUNCTION;
  RandomNumberGenerator &
  operator=(const RandomNumberGenerator &other) LLVM_DELETED_FUNCTION;
};
}

#endif
