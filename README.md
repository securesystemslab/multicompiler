LLVM Multicompiler Patches
==========================

This repo is based off the official LLVM git mirror:
http://llvm.org/git/llvm.git. We have added passes which randomize the
implementation details of the code to combat code-reuse attacks.

Installation
------------

Follow the LLVM instructions from
http://llvm.org/docs/GettingStarted.html#git-mirror but substitute this repo for
the llvm.git repo and https://github.com/securesystemslab/multicompiler-clang
for the clang.git repo.

Options
-------

### General Options

`-fdiversify` - Enable diversity. This is a shortcut for `-Xclang -nop-insertion`
and `-mllvm -sched-randomize`.

`-frandom-seed=S` - Set the random seed to some 64-bit unsigned int S. Set this
whenever using diversity options.

### NOP Insertion

`-Xclang -nop-insertion` - Enable the NOP insertion pass.

`-mllvm -nop-insertion-percentage=X` - Insert NOPs before X% of instructions (default 50%).

`-mllvm -max-nops-per-instruction=X` - Insert at most X NOPs each time (default 1).

### Schedule randomization

`-mllvm -sched-randomize` - Enable schedule randomization.

`-mllvm -sched-randomize-percentage=X` - Randomize X% of the instruction schedule (default 50%).
