
OUTFILE := bfjit

ARCH_RAW := $(shell uname -m)

ARCH_X86     := _ARCH_X86
ARCH_AARCH64 := _ARCH_AARCH64

ARCH_X86_FILES = $(wildcard asm/x86/*.cpp)
ARCH_AARCH64_FILES = $(wildcard asm/aarch64/*.cpp)

ifeq (${ARCH_RAW},x86_64)
ARCH := $(ARCH_X86)
NATIVE_FILES := $(ARCH_X86_FILES)
else ifeq ($(ARCH_RAW),aarch64)
ARCH := $(ARCH_AARCH64)
NATIVE_FILES := $(ARCH_AARCH64_FILES)
else ifeq ($(ARCH_RAW),arm64)
ARCH := $(ARCH_AARCH64)
NATIVE_FILES := $(ARCH_AARCH64_FILES)
else
$(error Unsupported platform '${ARCH_RAW}'. Exiting...)
endif

default: compile-native

compile-native:
	g++ -D$(ARCH) bf.cpp bfnodes.cpp asm/codeblob.cpp asm/bfcompiler.cpp $(NATIVE_FILES) -o $(OUTFILE)

emulate-x86:
	x86-linux-gnu-g++ -D$(ARCH_X86) -static bf.cpp bfnodes.cpp asm/codeblob.cpp asm/bfcompiler.cpp $(ARCH_X86_FILES) -o $(OUTFILE)

emulate-aarch64:
	aarch64-linux-gnu-g++ -D$(ARCH_AARCH64) -static bf.cpp bfnodes.cpp asm/codeblob.cpp asm/bfcompiler.cpp $(ARCH_AARCH64_FILES) -o $(OUTFILE)

mandelbrot:
	time ./$(OUTFILE) "$$(cat ./programs/mandelbrot.b)"

clean:
	rm $(OUTFILE)
