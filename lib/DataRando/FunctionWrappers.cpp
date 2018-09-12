//===- FunctionWrappers.cpp - Available wrappers for library functions ----===//

#include "llvm/DataRando/DataRando.h"
#include "llvm/DataRando/Runtime/Wrapper.h"
#include "llvm/DataRando/Runtime/DataRandoTypes.h"
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pcre.h>

// Macros for specializing TypeBuilder to struct types that may be present in
// the context.
#define S_STRUCT(TYPE)                                                  \
  template<bool xcompile> class TypeBuilder<struct TYPE, xcompile> {    \
  public:                                                               \
  static StructType *get(LLVMContext &Context) {                        \
    StructType *st = StructType::getByName(Context, "struct." #TYPE);   \
    if (st) {                                                           \
      return st;                                                        \
    }                                                                   \
    return StructType::create(Context, "struct." #TYPE);                \
  }                                                                     \
  }

#define S_TYPEDEF(TYPE)                                                 \
  template<bool xcompile> class TypeBuilder<TYPE, xcompile> {           \
  public:                                                               \
  static StructType *get(LLVMContext &Context) {                        \
    StructType *st = StructType::getByName(Context, "struct." #TYPE);   \
    if (st) {                                                           \
      return st;                                                        \
    }                                                                   \
    return StructType::create(Context, "struct." #TYPE);                \
  }                                                                     \
  }


#define S_UNION(TYPE)                                                   \
  template<bool xcompile> class TypeBuilder<TYPE, xcompile> {           \
  public:                                                               \
  static StructType *get(LLVMContext &Context) {                        \
    StructType *st = StructType::getByName(Context, "union." #TYPE);    \
    if (st) {                                                           \
      return st;                                                        \
    }                                                                   \
    return StructType::create(Context, "union." #TYPE);                 \
  }                                                                     \
  }

namespace llvm {

// specialize structs used in the wrappers
#if defined(__linux__)
S_STRUCT(_IO_FILE);
#elif defined(__APPLE__)
S_STRUCT(__sFILE);
#endif
S_STRUCT(addrinfo);
S_STRUCT(rlimit);
S_STRUCT(sockaddr);
S_STRUCT(timeval);
S_STRUCT(timezone);
S_STRUCT(pollfd);
S_STRUCT(iovec);
S_STRUCT(tm);
S_STRUCT(stat);
S_STRUCT(passwd);
S_STRUCT(sigaction);
S_STRUCT(real_pcre);
S_STRUCT(rusage);
S_STRUCT(tms);
S_UNION(pthread_cond_t);
S_UNION(pthread_condattr_t);
#if defined(__sigset_t_defined)
S_TYPEDEF(__sigset_t);
#endif
S_TYPEDEF(fd_set);

#define DR_WR(K, V, R, P) Wrappers.insert(std::make_pair(#K, WrapperInfo(#V, TypeBuilder<R P, false>().get(C))));

void FunctionWrappers::constructMap(LLVMContext &C) {
  DRRT_WRAPPERS
}

#undef DR_WR

bool FunctionWrappers::runOnModule(Module &M) {
  constructMap(M.getContext());

  CantEncryptTypes.insert(TypeBuilder<FILE*, false>().get(M.getContext()));

  MemManagement.insert("malloc");
  MemManagement.insert("free");
  MemManagement.insert("cfree");

  JmpFunctions.insert("setjmp");
  JmpFunctions.insert("_setjmp");
  JmpFunctions.insert("longjmp");
  JmpFunctions.insert("_longjmp");

  FormatFuncs = &getAnalysis<FormatFunctions>();

  // Initialize RTTIVtables. Add the mangled symbol names for the vtables for
  // the following classes:
  // __cxxabiv1::__fundamental_type_info
  // __cxxabiv1::__array_type_info
  // __cxxabiv1::__function_type_info
  // __cxxabiv1::__enum_type_info
  // __cxxabiv1::__class_type_info
  // __cxxabiv1::__si_class_type_info
  // __cxxabiv1::__vmi_class_type_info
  // __cxxabiv1::__pbase_type_info
  // __cxxabiv1::__pointer_type_info
  // __cxxabiv1::__pointer_to_member_type_info
  RTTIVtables.insert("_ZTVN10__cxxabiv123__fundamental_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv117__array_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv120__function_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv116__enum_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv117__class_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv120__si_class_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv121__vmi_class_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv117__pbase_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv119__pointer_type_infoE");
  RTTIVtables.insert("_ZTVN10__cxxabiv129__pointer_to_member_type_infoE");

  return false;
}

char FunctionWrappers::ID = 0;
static RegisterPass<FunctionWrappers> Z("function-wrappers", "Information about available function wrappers");
}
