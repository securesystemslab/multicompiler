# Multicompiler Build System Integration Notes

## Make

Conventional environment variables:

| Variable   | Description           | Example         |
|------------|-----------------------|-----------------|
| `CC`       | C compiler path       | `clang`         |
| `CXX`      | C++ compiler path     | `clang++`       |
| `AR`       | Archiver path         | `llvm-ar`^1     |
| `RANLIB`   | Archive indexer path  | `llvm-ranlib`^1 |
| `NM`       | Symbol lister path    | `llvm-nm`^1     |
| `CFLAGS`   | C compilation flags   |                 |
| `CXXFLAGS` | C++ compilation flags |                 |
| `LDFLAGS`  | Link flags            |                 |

^1 Needed for LTO only. GNU tools that accept `--plugin` should work instead of the LLVM variants.


## CMake

CMake configuration variables (passed to cmake with `-D...`).

| Variable                           | Description                      | Example          |
|------------------------------------|----------------------------------|------------------|
| `CMAKE_C_COMPILER`                 | C compiler                       | `clang`          |
| `CMAKE_CXX_COMPILER`               | C++ compiler                     | `clang++`        |
| `CMAKE_AR`                         | Archiver                         | `llvm-ar`^1      |
| `CMAKE_RANLIB`                     | Archive indexer                  | `llvm-ranlib`^1  |
| `CMAKE_NM`                         | Symbol lister                    | `llvm-nm`^1      |
| `CMAKE_C_FLAGS`                    | C compilation flags              |                  |
| `CMAKE_CXX_FLAGS`                  | C++ compilation flags            |                  |
| `CMAKE_EXE_LINKER_FLAGS`           | Exe link flags                   |                  |
| `CMAKE_SHARED_LINKER_FLAGS`        | Shared library link flags        |                  |
| `CMAKE_MODULE_LINKER_FLAGS`        | Module link flags                |                  |
| `CMAKE_BUILD_TYPE`                 | Build type                       | Release or Debug |
| `CMAKE_INSTALL_PREFIX`             | Installation target path         |                  |
| `CMAKE_POLICY_DEFAULT_CMP0056=NEW` | Might be required for CMake 3.2+ |                  |

^1 Needed for LTO only. GNU tools that accept `--plugin` should work instead of the LLVM variants.

`CMAKE_POLICY_DEFAULT_CMP0056=NEW` forces CMake 3.2+ to add
`CMAKE_EXE_LINKER_FLAGS` to the link when performing test compilations. See
https://cmake.org/cmake/help/v3.2/policy/CMP0056.html


## Libtool

`-flto` will need to be added to `CC`/`CXX` so that it is not stripped out by
libtool as an unrecognized flag. There may be cleaner ways to do this, but this
works.

`-XCClinker` forces libtool to pass the next flag on to `CC` when linking.

`-Xlinker` or `-Wl` will pass the next flag to the linker driver, prepended with
`-Wl` so it is passed through the driver to the linker itself.

`-Xcompiler` or `-Wc` will pass the next flag to the compiler unmodified.
