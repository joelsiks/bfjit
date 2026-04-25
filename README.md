
# BFJIT

BFJIT is an optimizing interpreter, Just-In-Time (JIT) compiler and Ahead-Of-Time (AOT) compiler for the [Brainfuck](https://en.wikipedia.org/wiki/Brainfuck) programming language. The JIT and AOT modes emit native instructions for either the x86 or AArch64 platform(s).

```shell
Usage: ./bfjit [--debug] [--aot|--jit|--interp] <program>
Execution mode defaults to JIT when not specified
```

This project is a way for me to give myself practical insight into how to build a basic JIT compiler for a small language with minimal features, focusing on the things I feel are most interesting: compiler design and learning more about assembly. Some of the features this project provides are:
* Tiered compilation design, interpreter, profiling, background JIT compilation, hot-swap to native code
* Emitting x86 and AArch64 instructions from scratch, considering calling convention, instruction encoding (assembling) and backpatching
* Cross-platform code generation targeting x86 and AArch64
* Optimizations for common Brainfuck patterns

The "memory" in a Brainfuck program is represented by a long "tape" of cells, along with a "pointer" to the current cell. For simplicity I've decided to limit the memory to 30000 cells, which cannot be dynamically re-allocated inside JIT/AOT compiled code.

As part of developing this I wrote a blog post on cache coherency, titled "JIT Compilers and Cache Coherency", available at: [https://joelsiks.com/posts/jit-compilers-and-cache-coherency/](https://joelsiks.com/posts/jit-compilers-and-cache-coherency/).

## Tiered Compilation

In JIT mode, only loops are compiled (or candidates for compilation). Loops contain a body of instructions, which might be other loops as well.

The JIT compilation model starts with all loops initially being interpreted, whereafter they are profiled to see how many times they are executed and how large they are. For large enough loops, and those that get executed often enough, a compilation request is sent to a background JIT thread that compiles the loop into a "compiled method". When the background thread is done, it inserts a reference to the compiled method, which gets picked up the next time it is interpreted and the compiled method is then called.

The main benefit from a tiered compilation approach is that code can start executing really quickly in the interpreter, and we can switch over to the compiled method once it is ready, which gives us good startup and warmup. In the AOT mode, all instructions are compiled into a native method before any code is executed, which makes startup a bit slower.

## Compilation Instructions

The default make target (`compile-native`) compiles a version of bfjit that is native to the system you're running on. For example, if I'm on an x86 system, bfjit will be compiled for x86.
```shell
$ make
$ make compile-native
```

It is also possible to cross-compile to a different platform by using any of the following `cross-compile-<platform>` targets. They require the appropriate cross-compiler to work, e.g., `aarch64-linux-gnu-g++` or `x86-linux-gnu-g++`. Right now the cross-compilation targets link the entire binary statically, meaning it will likely turn out larger than a native dynamically linked binary.
```shell
$ make cross-compile-aarch64
$ make cross-compile-x86
```

### Running Cross-Compiled Code

To emulate AArch64 on my x86 Ubuntu system, I've used the following tools:
```shell
$ sudo apt-get install qemu-system-arm gdb-multiarch
```

Cross-compiled AArch64 programs, like bfjit, can be run with user-mode emulation (not having to spin up an entire VM), and totally "transparently" if binfmt is set up correctly:
```shell
$ qemu-aarch64 ./bfjit ...

# Or check out binfmt:
$ cat /proc/sys/fs/binfmt_misc/qemu-aarch64
enabled
interpreter /usr/libexec/qemu-binfmt/aarch64-binfmt-P
flags: POF
offset 0
magic 7f454c460201010000000000000000000200b700
mask ffffffffffffff00fffffffffffffffffeffffff

$ uname -m
x86_64

$ file bfjit
bfjit: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=be96b3ed578e90474c0d10741b675e895e05a9ad, for GNU/Linux 3.7.0, not stripped

# No need for qemu-aarch64 with binfmt
$ ./bfjit ...
```

### Debugging Cross-Compiled Code

The `qemu-<platform>` command provides a handy `-g` flag that can be used to hook gdb into the emulator. This is extremely helpful in debugging. Credit to [Vega's ARM64 Guide](https://mariokartwii.com/arm64/ch28.html):
```shell
$ qemu-aarch64 -g 1234 ./bfjit ...

# Another terminal
$ gdb-multiarch -q --nh \
  -ex 'set architecture aarch64' \
  -ex 'target remote localhost:1234' \
  -ex 'layout split' \
  -ex 'layout regs'
```

### Example Compiled Method (x86)

Using the `--debug` flag we can inspect the compiled code.

```shell
$ ./bfjit --debug --aot "[++]"
Loop
 ByteInc (1)
 ByteInc (1)
After optimization:
Loop
 ByteInc (2)
Compiled method 0x7cd06f000000 size 27:
48 89 fe 8a 06 84 c0 0f
85 05 00 00 00 e9 08 00
00 00 80 06 02 e9 e9 ff
ff ff c3
```

Plugging it into a disassembler we can see the assembly code it represents:
```x86
0:  48 89 fe                mov    rsi,rdi
3:  8a 06                   mov    al,BYTE PTR [rsi]
5:  84 c0                   test   al,al
7:  0f 85 05 00 00 00       jne    0x12
d:  e9 08 00 00 00          jmp    0x1a
12: 80 06 02                add    BYTE PTR [rsi],0x2
15: e9 e9 ff ff ff          jmp    0x3
1a: c3                      ret
```

Following the System V ABI calling convention, the single argument that we pass to the compiled method arrives in the rdi register. The argument is a pointer to the current memory location, adjusted for the data pointer. As an optimization, we want to keep this pointer in the rsi register at all times, since syscalls expect a pointer to the memory it should operate on inside rsi, as the source buffer for a read or output buffer for a write. Therefore, the first thing we do is move the input argument from rdi to rsi.
```c++
typedef uint8_t* (*CompiledMethod)(uint8_t*);
```

## Performance

I've used the famous `mandelbrot.b` program to benchmark my implementation. These are numbers on my machine, YMMV:

**mandelbrot.b**:
| Execution Mode  | Time (s) |
| --------------- | -------- |
| Interpreter     | 33.21    |
| JIT             | 0.61     |
| AOT             | 0.60     |

## Future Work

* Implement more optimization passes for `BFOptimizer`, potentially speeding up performance even more by emitting fewer instructions.

* Investigate if adding an "assemble" phase which writes to memory once, is better than doing the write to memory on each emit.

## Resources

Some resources I've found helpful to learn about x86 and AArch64 assembly:

* x86:
  * [x86 and amd64 instruction reference](https://www.felixcloutier.com/x86/)
  * [X86-64 Instruction Encoding](https://wiki.osdev.org/X86-64_Instruction_Encoding)
  * [OpenCSF: 2.4. System Call Interface](https://w3.cs.jmu.edu/kirkpams/OpenCSF/Books/csf/html/Syscall.html)

* AArch64:
  * [A64 -- Base Instructions (alphabetic order)](https://www.scs.stanford.edu/~zyedidia/arm64/)
  * [Procedure Call Standard for the Arm® 64-bit Architecture (AArch64)](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
  * [AArch64/ARM64 Full Beginner's Assembly Tutorial](https://mariokartwii.com/arm64/)
