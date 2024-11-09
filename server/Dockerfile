FROM gcc:latest AS build
WORKDIR /app
COPY server/src server/src
COPY server/include server/include
COPY server/Makefile server/Makefile
COPY core/ core/
WORKDIR /app/server
RUN mkdir -p build
RUN gcc -static src/*.c ../core/*.c -o build/server -lm -lcrypto

FROM scratch
COPY --from=build /app/server/build/server /app/server
EXPOSE 8080
ENTRYPOINT [ "/app/server" ]
