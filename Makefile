all: uDaraChatServer

clean:
	rm -f uDaraChatServer

run: uDaraChatServer
	./uDaraChatServer


uDaraChatServer: uDaraChatServer.cpp
	g++  -march=native -O3 -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++20 -IuWebSockets/src -IuWebSockets/uSockets/src -flto=auto uDaraChatServer.cpp  uWebSockets/uSockets/*.o -lz -lssl -lcrypto -o uDaraChatServer

