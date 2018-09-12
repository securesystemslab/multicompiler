add_llvm_library(LLVMMCDisassembler
  Disassembler.cpp
  MCRelocationInfo.cpp
  MCExternalSymbolizer.cpp
  MCDisassembler.cpp

  DEPENDS
  intrinsics_gen
  )
