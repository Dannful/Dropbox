FROM gcc:latest AS build
WORKDIR /app
COPY client/src client/src
COPY client/include client/include
COPY client/Makefile client/Makefile
COPY core/ core/
WORKDIR /app/client
RUN mkdir -p build
RUN gcc -static src/*.c ../core/*.c -o build/client -lm -lcrypto

FROM scratch
COPY --from=build /app/client/build/client /app/client
WORKDIR /app
EXPOSE 8080
ENTRYPOINT [ "./client" ]
