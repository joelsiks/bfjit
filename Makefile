
OUTFILE := bfjit
ARCH_RAW := $(shell uname -m)

ARCH_X86     := _ARCH_X86
ARCH_AARCH64 := _ARCH_AARCH64

ifeq (${ARCH_RAW},x86_64)
ARCH := $(ARCH_X86)
else ifeq ($(ARCH_RAW),aarch64)
ARCH := $(ARCH_AARCH64)
else ifeq ($(ARCH_RAW),arm64)
ARCH := $(ARCH_AARCH64)
else
$(error Unsupported platform '${ARCH_RAW}'. Exiting...)
endif

default: compile

compile:
	g++ -D$(ARCH) bf.cpp bfnodes.cpp asm/codeblob.cpp asm/bfcompiler.cpp asm/x86/asm_x86.cpp asm/x86/bfcompiler_x86.cpp -o $(OUTFILE)

emulate-aarch64:
	aarch64-linux-gnu-g++ -D$(ARCH_AARCH64) -static bf.cpp bfnodes.cpp asm/codeblob.cpp asm/bfcompiler.cpp asm/aarch64/asm_aarch64.cpp asm/aarch64/bfcompiler_aarch64.cpp -o $(OUTFILE)

mandelbrot:
	time ./$(OUTFILE) "$$(cat ./programs/mandelbrot.b)"

clean:
	rm $(OUTFILE)
