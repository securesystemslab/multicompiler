include_directories(${CMAKE_SOURCE_DIR}/projects/poolalloc/include)

set(SOURCES
  DataRando.cpp
  DataChecks.cpp
  CSDataRando.cpp
  PointerEquivalenceAnalysis.cpp
  DataRandomizer.cpp
  FunctionWrappers.cpp
  MarkDoNotEncrypt.cpp
  HeapChecks.cpp
  )

add_llvm_loadable_module(DataRando ${SOURCES})
add_llvm_library(LLVMDataRando ${SOURCES})

add_dependencies(DataRando intrinsics_gen)
add_dependencies(LLVMDataRando intrinsics_gen)
