./build/server: ./build ./src/*.c ./include/*.h ./../core/*
	gcc -lm src/*.c ../core/*.c -o build/server

./build:
	mkdir -p build

run: ./build/server
	./build/server

clean:
	rm -rf build
