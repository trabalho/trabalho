all: clean compile run
compile:
	gcc -Wall shell.c -o shell
run: compile
	./shell
clean:
	rm -f shell

