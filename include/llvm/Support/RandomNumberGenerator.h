#ifndef RANDOMNUMBERGENERATOR_H_
#define RANDOMNUMBERGENERATOR_H_

#include "llvm/Config/config.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include <string>
#include <vector>

#if LLVM_ENABLE_OPENSSL
#include <openssl/aes.h>
#endif

#define AES_KEY_LENGTH 16 // bytes
#define AES_BLOCK_SIZE 16
#define PBKDF_ITERATIONS 1000

namespace llvm {

/* Random number generator based on either the AES block cipher from
 * openssl or an integrated linear congruential generator. DO NOT use
 * the LCG for any security application.
 */
class RandomNumberGenerator {
private:
  friend class Module;

  void Initialize(uint64_t Seed, StringRef Salt);

  /** Imports state file from disk */
  void ReadStateFile(StringRef StateFilename);

  /** Writes current RNG state to disk */
  void WriteStateFile(StringRef StateFilename);

  RandomNumberGenerator(RandomNumberGenerator const&)
    = delete;
  RandomNumberGenerator& operator=(RandomNumberGenerator const&)
    = delete;

  void Reseed(uint64_t Seed, StringRef Salt);

  // Internal state
#if LLVM_ENABLE_OPENSSL
  unsigned char IV[AES_BLOCK_SIZE];
  AES_KEY AESKey;
  unsigned char Key[AES_KEY_LENGTH];
  unsigned char EcountBuffer[AES_BLOCK_SIZE];
  unsigned int Num;
  unsigned char Plaintext[AES_KEY_LENGTH];
#else
  uint64_t state;
#endif

public:
  RandomNumberGenerator(StringRef Salt);
  RandomNumberGenerator(uint64_t Seed, StringRef Salt);

  uint64_t Random();
  uint64_t Random(uint64_t Max);

  // This function is DEPRECATED! Do not use unless you have NO access to a
  // Module to call createRNG() with.
  static RandomNumberGenerator& Generator() {
    static RandomNumberGenerator instance("");
    return instance;
  };

  ~RandomNumberGenerator();

  /**
   * Shuffles an *array* of type T.
   *
   * Uses the Durstenfeld version of the Fisher-Yates method (aka the Knuth
   * method).  See http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
   */
  template<typename T>
  void shuffle(T* array, size_t length) {
    if (length == 0) return;
    for (size_t i = length - 1; i > 0; i--) {
      size_t j = Random(i + 1);
      if (j < i)
        std::swap(array[j], array[i]);
    }
  }

  /**
   * Shuffles a SmallVector of type T, default size N
   */
  template<typename T, unsigned N>
    void shuffle(SmallVector<T, N>& sv) {
    if (sv.empty()) return;
    for (size_t i = sv.size() - 1; i > 0; i--) {
      size_t j = Random(i + 1);
      if (j < i)
        std::swap(sv[j], sv[i]);
    }
  }

  /**
   * Shuffles an SymbolTableList of type T
   */
  template<typename T>
    void shuffle(SymbolTableList<T>& list){
    if(list.empty()) return;
    SmallVector<T*, 10> sv;
    for(typename SymbolTableList<T>::iterator i = list.begin(); i != list.end(); ){
      /* iplist<T>::remove increments the iterator which is why the loop doesn't.
       */
      T* t = list.remove(i);
      sv.push_back(t);
    }
    shuffle<T*, 10>(sv);
    for(typename SmallVector<T*, 10>::size_type i = 0; i < sv.size(); i++){
      list.push_back(sv[i]);
    }
  }

  /**
   * Shuffles a Vector of type T
   */
  template<typename T>
  void shuffle(std::vector<T>& v) {
    if (v.empty()) return;
    for (size_t i = v.size() - 1; i > 0; i--) {
      size_t j = Random(i + 1);
      if (j < i)
        std::swap(v[j], v[i]);
    }
  }
};


}

#endif
