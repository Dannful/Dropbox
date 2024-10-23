./build/client: ./build ./src/*.c ./include/*.h ./../core/*
	gcc -lm src/*.c ../core/*.c -o build/client

./build:
	mkdir -p build

run: ./build/client
	./build/client

clean:
	rm -rf build
