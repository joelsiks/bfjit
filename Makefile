
OUTFILE=bfjit

compile:
	g++ bf.cpp asm.cpp -o bfjit

compile-debug:
	g++ -g bf.cpp asm.cpp -o bfjit

mandelbrot:
	time ./$(OUTFILE) "$$(cat ./programs/mandelbrot.b)"

clean:
	rm $(OUTFILE)
