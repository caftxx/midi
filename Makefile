all: a.o convert

clean:
	rm -f a.o convert

a.o:
	gcc -o a.o main.c midi.c -lm -g

convert:
	gcc -o convert convert.c midi.c -lm -g
