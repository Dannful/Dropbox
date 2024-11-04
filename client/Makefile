./build/client: ./build ./src/*.c ./include/*.h ./../core/*
	gcc src/*.c ../core/*.c -o build/client -lm

./build:
	mkdir -p build

run: ./build/client
	./build/client

clean:
	rm -rf build
