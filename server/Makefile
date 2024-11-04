./build/server: ./build ./src/*.c ./include/*.h ./../core/*
	gcc src/*.c ../core/*.c -o build/server -lm

./build:
	mkdir -p build

run: ./build/server
	./build/server

clean:
	rm -rf build
