
# BFJIT

BFJIT is an optimizing Interpreter/Just In Time/Ahead Of Time execution for the [Brainfuck](https://en.wikipedia.org/wiki/Brainfuck) programming language. The Just In Time (JIT) and Ahead Of Time (AOT) modes emit code only for the x86 platform.

```shell
Usage: ./bfjit [--debug] [--aot|--jit|--interp] <program>
Execution mode defaults to JIT when not specified
```

I've done this project as a way to give myself practical insight into how to build a basic JIT compiler for a small language will minimal features, focusing on the things I feel are most interesting: compiler design and learning more about assembly. Some of the features this project provides are:
* Tiered compilation design, interpreter, profiling, background JIT compilation, hot-swap to native code
* Emitting x86 instructions from scratch, considering calling convention, REX, SIB and ModRM encoding and backpatching jumps
* Optimizations for common Brainfuck patterns

The "memory" in a Brainfuck program is represented by a long "tape" of cells, along with a "pointer" to the current cell. For simplicity I've decided to limit the memory to 30000 cells, which cannot be dynamically re-allocated inside the JIT/AOT compiled code.

## Tiered Compilation

In the JIT execution mode, the only thing that is (or might be) compiled are loops. Loops contain a body of other instructions, which might be other loops as well, which I refer to as nested loops.

The model for JIT compilation is that all loops start by being interpreted, whereby they are profiled to see how many times they are being executed and how large they are. For large enough methods, and those that get executed enough times, a compilation request is sent to a background JIT thread that compiles the loop into a "compiled method". When the background thread is done, it inserts a reference to the compiled method to the loop, which get's picked up the next time it is interpreted and the compiled method is called.

The main benefit from a tiered compilation approach is that code can start executing really quickly in the interpreter, and we can switch over to the compiled method once it is ready, which gives us good startup and warmup. In the AOT mode, all instructions are compiled into a native method before any code is executed, which makes startup a bit slower.

### Example Compiled Method

Using the `--debug` flag we can inspect the compiled code.

```shell
./bfjit --debug --aot "[++]"
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

Following the System V ABI calling convention, the single argument that we pass to the compiled method arrives in the rdi register. The argument is a pointer to the current memory location, adjusted for the data pointer. As an optimization, we want to keep this pointer in the rsi register at all times, since syscall instruction expect a pointer to the memory it should operate on (insert to in a read or output from in a write), so the first thing we do is move it to rsi.
```c++
typedef uint8_t* (*CompiledMethod)(uint8_t*);
```

## Performance

I've used the famous `mandelbrot.b` program to benchmark my implementation. These are numbers on my machine, YMMV:

**mandelbrot.b**:
| Execution Mode  | Time     |
| --------------- | -------- |
| Interpreter     | 33.21    |
| JIT             | 0.61     |
| AOT             | 0.60     |

## Future Work

* Implement more optimization passes for `BFOptimizer`, potentially speeding up performance even more by emitting fewer instructions.
* Implement support for emitting native instructions for other platforms (maybe aarch64) in addition to x86 and decouple the design from x86 into a more general API.

## Resources

Some resources I've used to learn about x86 assembly that I've found helpful:
 * [x86 and amd64 instruction reference](https://www.felixcloutier.com/x86/)
 * [X86-64 Instruction Encoding](https://wiki.osdev.org/X86-64_Instruction_Encoding)
 * [OpenCSF: 2.4. System Call Interface](https://w3.cs.jmu.edu/kirkpams/OpenCSF/Books/csf/html/Syscall.html)
