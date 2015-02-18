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
#include "llvm/Support/RandomNumberGenerator.h"

using namespace llvm;

// Tracking BUG: 19665
// http://llvm.org/bugs/show_bug.cgi?id=19665
//
// Do not change to cl::opt<uint64_t> since this silently breaks argument
// parsing.
static cl::opt<unsigned long long>
    Seed("rng-seed", cl::value_desc("seed"),
         cl::desc("Seed for the random number generator"), cl::init(0));

RandomNumberGenerator::RandomNumberGenerator(StringRef Salt) {
  DEBUG(if (Seed == 0) dbgs()
        << "Warning! Using unseeded random number generator.\n");

  // Combine seed and salts using std::seed_seq.
  // Data: Seed-low, Seed-high, Salt
  // Note: std::seed_seq can only store 32-bit values, even though we
  // are using a 64-bit RNG. This isn't a problem since the Mersenne
  // twister constructor copies these correctly into its initial state.
  std::vector<uint32_t> Data;
  Data.reserve(2 + Salt.size());
  Data.push_back(Seed);
  Data.push_back(Seed >> 32);

  std::copy(Salt.begin(), Salt.end(), Data.end());

  std::seed_seq SeedSeq(Data.begin(), Data.end());
  Generator.seed(SeedSeq);
}

RandomNumberGenerator::result_type RandomNumberGenerator::operator()() {
  return Generator();
}

// This function is derived from OpenBSD's libc, original license
// follows:
//
// Copyright (c) 2008, Damien Miller <djm@openbsd.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

// Calculate a uniformly distributed random number less than upper_bound
// avoiding "modulo bias".
//
// Uniformity is achieved by generating new random numbers until the one
// returned is outside the range [0, 2**32 % upper_bound).  This
// guarantees the selected random number will be inside
// [2**32 % upper_bound, 2**32) which maps back to [0, upper_bound)
// after reduction modulo upper_bound.
RandomNumberGenerator::result_type
RandomNumberGenerator::operator()(result_type upper_bound) {
  result_type r, min;

  if (upper_bound < 2)
    return 0;

  /* 2**32 % x == (2**32 - x) % x */
  min = -upper_bound % upper_bound;

  /*
   * This could theoretically loop forever but each retry has
   * p > 0.5 (worst case, usually far better) of selecting a
   * number inside the range we need, so it should rarely need
   * to re-roll.
   */
  for (;;) {
    r = Generator();
    if (r >= min)
      break;
  }

  return r % upper_bound;
}
