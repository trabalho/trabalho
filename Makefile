all: clean compile run
compile:
	gcc -wall ProjetoSO2.h -o main
	gcc -Wall main.c -o main
run: compile
	./main
clean:
	rm -f main

