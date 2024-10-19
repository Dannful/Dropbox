./build/server: ./build ./src/*.c
	gcc src/*.c -o build/server

./build:
	mkdir -p build

run: ./build/server
	./build/server

clean:
	rm -rf build
