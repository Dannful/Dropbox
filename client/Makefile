./build/client: ./build ./src/*.c
	gcc src/*.c -o build/client

./build:
	mkdir -p build

run: ./build/client
	./build/client

clean:
	rm -rf build
