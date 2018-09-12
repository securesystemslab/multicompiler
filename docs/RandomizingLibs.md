# Compiling shared libraries with vtable-rando

If a target application for vtable-rando is calling virtual methods in shared libraries, the shared libraries should also be compiled by vtable-rando. Especially, C++ standard library contains a lot of virtual methods and using it is almost inevitable in C++ programming. The following explains how to compile the C++ standard libraries with vtable-rando. We can use either `libc++` or `libstdc++` but `libc++` is recommended because `libstdc++` is only known to work in the specific environment.

## Option 1: libc++ (recommended)

#### Get `libcxxabi` and `libcxx` source:
```
git clone -b release_38 http://llvm.org/git/libcxxabi.git
git clone -b release_38 http://llvm.org/git/libcxx.git
```

#### Build `libcxxabi` (libcxx is dependent on `libcxxabi`):

*NOTE: This example is assuming that you are installing libcxx to the same path where libcxxabi has been installed for simplicity.*

```
cd libcxxabi
mkdir build
cd build
cmake .. \
-DLIBCXXABI_LIBCXX_PATH=path-to-libcxx-source \
-DCMAKE_C_COMPILER=multicompiler-path/clang \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CXX_COMPILER=multicompiler-path/clang++ \
-DLLVM_PATH=path-to-multicompiler-source \
-DCMAKE_C_FLAGS="-fvtable-rando" \
-DCMAKE_CXX_FLAGS="-fvtable-rando" \
-DCMAKE_EXE_LINKER_FLAGS="-fvtable-rando -Wl,--plugin-opt,-random-seed=1" \
-DLIBCXXABI_LIBCXX_INCLUDES=path-to-libcxx-source/include \
-DCMAKE_INSTALL_PREFIX=path-to-install-libcxx
make && make install
```

#### Build `libcxx`:
```
cmake .. \
-DCMAKE_C_COMPILER=multicompiler-path/clang \
-DCMAKE_CXX_COMPILER=multicompiler-path/clang++ \
-DCMAKE_INSTALL_PREFIX=path-to-install-libcxx \
-DCMAKE_BUILD_TYPE=Release -DLIBCXX_CXX_ABI=libcxxabi \
-DLIBCXX_CXX_ABI_INCLUDE_PATHS=path-to-install-libcxx/include \
-DLLVM_PATH=path-to-multicompiler-source \
-DLIBCXX_CXX_ABI_LIBRARY_PATH=path-to-install-libcxx/lib \
-DCMAKE_C_FLAGS="-fvtable-rando" \
-DCMAKE_CXX_FLAGS="-fvtable-rando" \
-DCMAKE_EXE_LINKER_FLAGS="-fvtable-rando -Wl,--plugin-opt,-random-seed=1"
```

*NOTE: Do not locate the custom libc++ in the same path to multicompiler unless you've built multicompiler with vtable-rando.*

For more information, see: http://libcxx.llvm.org/docs/BuildingLibcxx.html


#### Linking against the randomized `libcxx`:

After you compiled libc++ with multicompiler and vtable-rando option, you should link your target application against the custom-built libc++. 

In order to do that, you should pass the following flags to compiler and linker respectively. 

For compiler: `-nostdinc++ -I{path-to-install-libcxx}/include/c++/v1 -L{path-to-install-libcxx}/lib`

For linker: `-stdlib=libc++ -Wl,-rpath,path-to-install-libcxx/lib`

For more information, see: http://libcxx.llvm.org/docs/UsingLibcxx.html


## Option 2: libstdc++

*NOTE: This is only known to work with gcc-4.8 on Ubuntu-14.04.*

#### Get `gcc-4.8` source:

```
git clone -b gcc-4_8_4-release git://gcc.gnu.org/git/gcc.git gcc-4_8_4
cd gcc-4_8_4
git checkout -b gcc-4_8_4-release
```

#### Build and install `libstdc++`:

```
cd libstdc++-v3
mkdir build && cd build
../configure CC=multicompiler-path/clang CXX=multicompiler-path/clang++ CXXFLAGS='-frandom-seed=1 -fvtable-rando' LDFLAGS='-fvtable-rando' --prefix=path-to-install-custom-libstdc++ --disable-multilib --disable-nls 
make && make install
```

*NOTE: Do not install the custom libstdc++ alongside the multicompiler unless you've built multicompiler with vtable-rando.*

*NOTE: when configuring libstdc++, make sure that the gthread headers (`gthr.h`, `gthr-default.h`, etc.) are found. On a stock Ubuntu 14.04 you might have to add `-I/usr/include/x86_64-linux-gnu/c++/4.8/bits` to `CXXFLAGS`. Failure to do so will prevent multi-threaded C++ applications from building.*

#### Linking against the randomized `libstdc++`:

After you compiled libstdc++ with multicompiler and vtable-rando option, you should link your target application against the custom-built libstdc++. In order to do that you should add the following flags: 

To the compiler: `-I{path-to-install-custom-libstdc++}/include/c++/4.8.4/ -L{path-to-install-custom-libstdc++}/lib`

To the linker: `-Wl,-rpath,path-to-install-custom-libstdc++/lib`
