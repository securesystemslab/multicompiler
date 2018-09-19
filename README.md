LLVM Multicompiler
==========================

This repo is based off the official LLVM git mirror:
http://llvm.org/git/llvm.git. We have added passes which randomize the
implementation details of the code to combat code-reuse attacks.

Test Environment
-----------------
Ubuntu 14.04 LTS 64 bit

gcc 4.8.4

Python 2.7.6

GNU M4 1.4.17

GNU Autoconf 2.69

GNU Automake 1.14.1

libtool 2.4.2

zlib 1.2.3.4

binutils 2.24 (build instructions below)

Installation
------------


Below are all instructions explained above.

#### Installing prerequisites

`apt-get install -y libssl-dev libxml2-dev libpcre3-dev`

#### Checking out LLVM, Clang, compiler-rt, poolalloc, and SVF:

`git clone git@github.com:securesystemslab/multicompiler.git llvm`

`git clone git@github.com:securesystemslab/multicompiler-clang.git llvm/tools/clang`

`git clone git@github.com:securesystemslab/multicompiler-compiler-rt.git llvm/projects/compiler-rt`

`git clone git@github.com:securesystemslab/poolalloc.git llvm/projects/poolalloc`

### Link-Time Optimization (LTO)

Function randomization requires link-time optimization, since LTO ensures that functions are shuffled not within each source file but across the whole program, achieving higher entropy.

#### Installing prerequisites

`apt-get install -y flex bison texinfo`

#### Building binutils with Gold for LLVM LTO support and global shuffling

For Binutils 2.26, clone:

`git clone git@github.com:/securesystemslab/binutils.git`

Binutils 2.24 is also available in the `cfar-2_24` branch

If you want to use Readactor execute-only features, apply the Readactor Gold patch now:

`patch -p1 < PATH_TO_READACTOR_SOURCES/linker/binutils-gold-xonly.patch`

Configure binutils with these flags:

`--enable-gold --enable-plugins --prefix=prefix --disable-werror`

(prefix is not required, you can run `gold/ld-new` directly from the build directory by symlinking or copying it to the LLVM build/install binary directory.)

The randomizing gold patch requires openssl < v1.1. To explicitly build against
openssl-1.0, add the following environment variables before _both_ the configure
and make commands: `CPPFLAGS="-I/usr/include/openssl-1.0"
LDFLAGS="-L/usr/lib/openssl-1.0"`.

then

`make`

`make install`


## Building and Compiling LLVM

### Patch printf.h from glibc:

There is a bug in glibc printf.h:
https://sourceware.org/bugzilla/show_bug.cgi?id=18907. Apply the following
patch to /usr/include/printf.h to fix the bug.

```diff
--- /usr/include/printf.h.orig	2016-12-13 21:34:35.897301441 +0000
+++ /usr/include/printf.h	2016-12-13 21:35:40.374031243 +0000
@@ -111,13 +111,13 @@
    it returns a positive value representing the bit set in the USER
    field in 'struct printf_info'.  */

-extern int register_printf_modifier (const wchar_t *__str) __wur __THROW;
+extern int register_printf_modifier (const wchar_t *__str) __THROW __wur;


 /* Register variable argument handler for user type.  The return value
    is to be used in ARGINFO functions to signal the use of the
    type.  */
-extern int register_printf_type (printf_va_arg_function __fct) __wur __THROW;
+extern int register_printf_type (printf_va_arg_function __fct) __THROW __wur;


 /* Parse FMT, and fill in N elements of ARGTYPES with the
```

### **Recommended**: Building LLVM and Clang using **cmake**:

#### Linux

1. `cmake .. -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_INSTALL_PREFIX=... -DCMAKE_BUILD_TYPE=Release -DLLVM_BINUTILS_INCDIR=...`

To build cross-checking support, add the following to the previous command
`-DMULTICOMPILER_RAVEN_SRCDIR=<raven sources>
-DMULTICOMPILER_RAVEN_OUTDIR=<raven build>` where `<raven sources>` is the directory
containing the raven MVEE source code (containing `rbuff/rbuff.h`) and `<raven
build>` is the directory containing the built raven MVEE (containing
`rbuff/librbuff.so`).

The multicompiler RNG requires openssl < v1.1. If you have both openssl-1.0 and
openssl-1.1 installed, you need to force cmake to recognize the 1.0 version with
the following additional arguments:
`-DOPENSSL_INCLUDE_DIR=/usr/include/openssl-1.0
-DOPENSSL_CRYPTO_LIBRARY=/usr/lib/libcrypto.so.1.0.0`. If you wish to enable
`LLVM_OPTIMIZED_TABLEGEN`, you'll also need to patch `NATIVE/CMakeCache.txt` in
the build directory manually after the build fails because there is no good way
to set cmake variables for the second cmake invocation the LLVM build system
uses to build optimized tablegen for a debug build. Manually overwrite
`OPENSSL_CRYPTO_LIBRARY` and `OPENSSL_INCLUDE_DIR` with the values from
`CMakeCache.txt`. This has been tested on current Arch Linux, exact paths may
vary for other distributions.

2. `make`

#### macOS

To build on macOS, you may need to export `OPENSSL_ROOT_DIR` so it points to your openssl root folder. Homebrew puts openssl in `/usr/local/opt/openssl`.

1. Export `OPENSSL_ROOT_DIR=/path/to/openssl`.

2. `cmake .. -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_INSTALL_PREFIX=... -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-I$OPENSSL_ROOT_DIR/include -DCMAKE_CXX_FLAGS=-I$OPENSSL_ROOT_DIR/include`


### Deprecated: Building LLVM and Clang using **configure**:

`cd llvm`

`mkdir build`

`cd build`

`../configure --prefix=... --enable-optimized --enable-targets=x86,x86_64`

`make`

`make install`

Options:

The release version is optimized. Switch `--enable-optimized with --disable-optimized` for a debug (10x slower!!) build.

For LTO:

1. use `--with-binutils-include=<binutils 'include' path>`

2. to install in your own prefix directory, make sure that both binutils and LLVM have the same prefix, then make install both toolchains to the same prefix.

3. also, copy or link from LLVMgold.so to `prefix/lib/bfd-plugins/LLVMgold.so` and make sure `prefix/bin/ld` points to `ld.gold`, not `ld.bfd`.

`--with-built-clang` instructs the test-suite to use the built version of clang rather than llvm-gcc.

For 64-bit targets, use `--enable-targets=x86_64` or `--enable-targets=x86,x86_64`.

Note: On some systems the prefix path must be absolute: `/home/myuser/multicompiler/install` rather than `../install`.


Options
-------

For LTO, all `-mllvm -option-here` options should be translated to the form `-Wl,--plugin-opt,-option-here` and `-flto` added to the compilation (and potentially linking, if they are different) flags.

*important:* When using build systems that use libtool (e.g. Apache), you must include `-flto` in the *compiler* path (`CC`), rather than `CFLAGS`/`LDFLAGS`. Libtool (stupidly and silently) strips unrecognized options such as `flto` from the `CFLAGS`/`LDFLAGS`. Example of correct usage: `export CC="/path/to/multicompiler/clang -flto"`

### General Options

`-frandom-seed=#` - Set the random seed to #.(The type of # is uint64_t, which means the range should be within 0 - (2^64-1))

For LTO: `-Wl,--plugin-opt,-random-seed=#`

### Stack-layout randomization and reversal

`-mllvm -shuffle-stack-frames` - Enable stack-layout randomization.

`-mllvm -reverse-stack-frames` - Reverse layout of each stack frame.

`-mllvm -stack-frame-random-seed=SEED` - Distinct stack frame randomization seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization (and stack frame padding).

### Insert padding between stack frames

`-mllvm -max-stack-pad-size=#` - Enable inter-stack-frame padding of which the maximum size is #. (The preferable size is between 0 and 256)

`-mllvm -stack-frame-random-seed=SEED` - Distinct stack frame randomization seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization (and stack frame shuffling).

### Stack-element padding
Insert padding between elements in the unsafe stack.

`-mllvm -stack-element-percentage=#` - Percentage of elements in the unsafe stack prepended by paddings.

`-mllvm -max-stack-element-pad-size=#` - Maximum size of stack element padding (The preferable size is between 0 and 50).

`-mllvm -stack-element-padding-random-seed=SEED` - Distinct stack element padding randomization seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization.

### Function randomization (LTO req'd)
Using this transformation **without LTO** is possible but **not recommended**.

`-mllvm -randomize-function-list` - Enable function randomization.

### Machine register randomization

`-mllvm -randomize-machine-registers` - Enable machine register randomization.

### Safestack

`-fsanitize=safe-stack` - Enable Safestack. This feature places buffers and other "address-taken" variables on a separate stack to prevent stack smashing.

NOTE: this option should be passed to both compiler and linker as any other sanitizer flags.

### Stack-to-heap promotion

`-mllvm -stack-to-heap-promotion` - Enable stack-to-heap promotion: this feature randomly promotes buffers in stack slots to heap.

`-mllvm -stack-to-heap-percentage=#` - Percentage of buffers to be promoted to heap.

The promoted slots are malloc'ed in the beginning and then free'd when function returns (performance concerns).
In the current implementation, the promoted stack slots and their pointers are not remained in the stack.

TODO: 1) This currently does not promote buffers in safestack if it is used with SafeStack. 2) Promoting DynamicAllocas is currently not supported.

NOTE: This transformation should not be applied to signal handlers because it inserts async-signal-unsafe functions: malloc() and free(). To avoid this issue, we whitelist signal handlers defined in [ATDSigHandlers.def](https://github.com/securesystemslab/multicompiler/blob/master/lib/CodeGen/ATDSigHandlers.def).

### Code-pointer protection (LTO req'd)

`-fcode-pointer-protection` - Replaces code pointers with pointers to trampolines. Prevents code pointers from leaking the code layout. Cookies are used to authenticate calls and returns through trampolines. Intended to be used in conjunction with X-only memory. Note: this feature works with stack unwinding but **likely breaks C++ exception handling**.

For LTO: `-Wl,--plugin-opt,-pointer-protection -Wl,--plugin-opt,-call-pointer-protection -Wl,--plugin-opt,-cookie-protection`

Function pointer trampolines support striding to support disjoint trampoline
table indices. By carefully choosing relatively prime offsets for each variant,
we can ensure that the same offset from a give trampoline will not be a valid
trampoline in all variants.

`-mllvm -disjoint-trampoline-spacing=M` - Space trampolines apart by M table indices (M*8 bytes on X86).

`-mllvm -disjoint-trampoline-multiple=N` - Do not emit a trampoline at multiples of index N

With these two options together, we can create disjoint trampoline tables. For
example: set `-disjoint-trampoline-spacing` to 3, 4, 5, and 7 in four variant
respectively, and set `-disjoint-trampoline-multiple=420` to avoid emitting
trampolines to all common multiples of those offsets.

### Global padding (LTO req'd)
Using this transformation **without LTO** is possible but **not recommended**.

`-mllvm -global-padding-percentage=N` - Add a randomly size padding global for N% of global variables.

`-mllvm -global-padding-max-size=SIZE` - Maximum size of global variable padding (size is randomly chosen from (0,SIZE] ).

`-mllvm -global-min-count=N` - Ensure that there are at least N global variables. If the input (plus any additional padding globals inserted via `-global-padding-percentage` above) contains more than 1 but fewer than N globals, add enough randomly sized padding globals to ensure N globals. This is particularly useful when shuffling globals to ensure there is sufficient entropy. This option treats normal and common globals separately and ensures there are at least N of each, since these global lists are shuffled independently.

`-mllvm -global-randomization-random-seed=SEED` - Distinct global randomization seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization (and global shuffling, below).

### Global Shuffling and reversal (LTO req'd)
Using this transformation **without LTO** is possible but **not recommended**. This transformation requires a patched version of gold for complete coverage, see below.

`-mllvm -shuffle-globals` - Randomly permute the ordering of global variables.

`-mllvm -reverse-globals` - Reverse the ordering of global variables.

`-mllvm -global-randomization-random-seed=SEED` - Distinct global randomization seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization (and global padding, above).

Common global symbols, i.e. the compiler was unsure where the global was defined and therefore allocated, cannot be randomized in LLVM/Clang. The linker actually defines and allocates these objects. Therefore, we have to use a patched linker to enforce randomization of these variables. With the patched gold mentioned above, add the following linker flags for common symbol randomization:

`-Wl,--sort-common=random` - Sort the common variables randomly.

`-Wl,--sort-common=random-reverse` - Sort the common variables randomly and then reverse their order.

`-Wl,--random-seed=SEED` - Random seed passed for the linker.

### NOP Insertion
Insert NOP instructions before some instructions.

 `-Xclang -nop-insertion` - Enable NOP insertion.

`-mllvm -nop-insertion-percentage=#` - Percentage of instructions prepended by NOP instructions.

`-mllvm -max-nops-per-instruction=#` - Maximum number of NOP insertion for a instruction.

`-mllvm -NOP-random-seed=#` - Distinct NOP insertion seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization.

### MOV-to-LEA
Change “MOV r1, r2” to the equivalent “LEA r1, [r2]".

`-mllvm -mov-to-lea-percentage=#` - Percentage of “MOV” instructions that are changed to “LEA” instructions.

`-mllvm -MOVToLEA-random-seed=#` - Distinct “MOV to LEA” seed. Overrides `-frandom-seed` (or `-random-seed` above) for this randomization.

### VTable randomization (Linux only)
Split vtable into read-only part (rvtable) and randomized execute-only part (xvtable).

`-fvtable-rando` - Enable VTable randomization including booby trap insertion.

NOTE: this option should be passed to both compiler and linker to link against compiler-rt (libclang-rt.vtable_rando-$arch.so).

This includes the following defaults, which can be changed by adding them after `-fvtable-rando`:

`-Xclang -min-number-vtable-entries=10` - Pad each vtable to have at least 10 entries. Too few entries will have insufficient entropy for effective randomization.

`-Xclang -min-percent-vtable-boobytraps=25` - Insert boobytrap vtable entries so that at least 25% of the vtable is boobytrapped.

With LTO, `-Wl,--plugin-opt,-mark-vtables` is required to enable marking of
VTables in IR.

#### Usage Notes:

1. Vtables are randomized at runtime, therefore, the randomization seed and reversal option is controlled via environment variables. Use `MVEE_VTABLE_RANDO_SEED` to specify the seed (must fit into an `unsigned long`) and set `MVEE_VTABLE_RANDO_REVERSE` to any value to reverse the shuffled vtable layout.
2. Vtable randomization appears to work on Ubuntu 14.04 x86-64 but generates errors on Ubuntu 16.04 x86-64. The feature was tested in isolation in googletest-1.8.0; beware of interactions with other randomization and/or compilation options. 
3. Determining that vtable randomization was applied correctly: 
    - `ldd /path/to/your/binary` should include `libclang_rt.vtable_rando-x86_64.so` in its output.
    - `nm /path/to/your/binary` should include `vtablerando_randomize` and `vtablerando_register_module`.
4. RTTI is required for VTable randomization, so do NOT include `-fno-rtti` in
   the C++ build flags.

#### Building shared libraries for VTable randomization:

If a target application for vtable-rando is calling virtual methods in shared libraries, the shared libraries should also be compiled by vtable-rando. The detailed instructions how to build and link against the libraries are [here](docs/RandomizingLibs.md).

### PLT randomization (Linux only)
Shuffles the procedure linkage table at load time.

`-fplt-rando` - Enable PLT randomization.

NOTE: this option should be passed to both compiler and linker to link against compiler-rt (libclang-rt.plt_rando-$arch.so).

#### Usage Notes:

1. PLTs are randomized at run time, therefore, the randomization seed and reversal option is controlled via environment variables. Use `PLT_RANDO_SEED` to specify the seed (must fit into an `unsigned long`) and set `OKT_RANDO_REVERSE` to any value to reverse the shuffled PLT layout.
2. Determining that PLT randomization was applied correctly: 
    - `ldd /path/to/your/binary` should include `libclang_rt.plt_rando-x86_64.so` in its output.
    - `nm /path/to/your/binary` should include `pltrando_randomize` and `pltrando_register_module`.

### Data randomization (LTO req'd, Linux only)
Randomize data representations by using xor encryption and description with random masks.
This is only known to work with Apache 2.0.4 and thttpd.

`-data-rando` - Enable data randomization pass.

`-cs-data-rando` - Enable context sensitive data randomization pass.

For LTO: `-fdata-rando` - Context insensitive data randomization
         `-fcs-data-rando` - Context sensitive data randomization

Datarando does not always function correctly when SLP vectorization is
enabled. To disable the SLP vectorizer use _both_ of the following options:

During compilation (CFLAGS): `-fno-slp-vectorize`
During LTO linking (LDFLAGS): `-Wl,-plugin-opt,disable-vectorization`

### Data crosschecking
Insert raven crosschecks before branching based on potentially encrypted
booleans. This pass is now run by clang during regular compilation, before LTO.

`-fsanitize=crosscheck` - Enable data crosschecking pass
`-mllvm -xcheck-data` - Enable data cross checks (requires `-fsanitize=crosscheck`)

The sanitize option is required during both compilation and linking.

To force cross checks at branches instead of after loading cross-checked values,
additionally use: `-mllvm -data-checks-at-branch` (required when combined with struct
layout randomization). This requires the previous two options as well.

Periodic cross-checking is enabled by default. To disable, configure cmake with
`-DMULTICOMPILER_PERIODIC_CROSSCHECKS=On`, however, this should not be necessary
in normal use.

Linking against the synchronous version of the cross-checking runtime for
debugging is enabled by linking with `-fsanitize-debug-crosscheck` along with
the usual `-fsanitize=crosscheck`. Additional logging of crosschecks for
debugging can be enabled with `-mllvm -log-xchecks`.

Further details of data randomization and crosschecking are [here](docs/DataRando.org).

Functions can be blacklisted from crosschecking by adding them to a `-fsanitize`
blacklist (see
http://releases.llvm.org/3.8.1/tools/docs/SanitizerSpecialCaseList.html)

### Control flow crosschecking
Insert raven crosschecks at the beginning of each function to ensure that the
same function sequence is called in each variant.

`-fsanitize=crosscheck` - Enable data crosschecking pass (required for control flow checks)
`-mllvm -xcheck-cf` - Enable function-level control flow crosschecking

Can be freely combined with data crosschecks, just use a single
`-fsanitize=crosscheck` option.

Control-flow crosschecks respect the same sanitizer blacklist as data
crosschecks (see above). Blacklist functions with `fun:foo` in the blacklist to
not insert crosschecks into function `foo`.
