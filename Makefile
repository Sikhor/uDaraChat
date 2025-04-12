all: uDaraChatServer

clean:
	rm -f uDaraChatServer

run: uDaraChatServer
	./uDaraChatServer


uDaraChatServer: uDaraChatServer.cpp ChatLibrary.o 
	g++  -march=native -O3 -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++20 -IuWebSockets/src -IuWebSockets/uSockets/src -flto=auto uDaraChatServer.cpp  ChatLibrary.cpp uWebSockets/uSockets/*.o -lz -lssl -lcrypto -o uDaraChatServer


ChatLibrary.o: ChatLibrary.cpp ChatLibrary.h
	g++ -march=native -O3 -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++20 -IuWebSockets/src -IuWebSockets/uSockets/src -flto=auto -c ChatLibrary.cpp

