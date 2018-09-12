set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})

# pl: not sure this is safe, but llc is defined elsewhere,
# so rename to ld-randomize-script.
#add_llvm_tool(ld-randomize-script
add_llvm_tool(ld-randomize-script
  ld-randomize-script.cpp
  )
