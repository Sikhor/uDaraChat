all: uDaraChatServer uDaraChatBot

clean:
	rm -f uDaraChatServer uDaraChatBot *.o

run: uDaraChatServer uDaraChatBot
	./uDaraChatServer


CXXFLAGS = -march=native -O3 -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++20 -I. -IuWebSockets/src -IuWebSockets/uSockets/src -flto=auto
LIBS = -lz -lssl -lcrypto
CXXCLIENTFLAGS= -O0 -g -std=c++17 -I. 
CLIENTLIBS = -pthread

uDaraChatServer: uDaraChatServer.cpp ChatLibrary.o GroupContainer.o
	g++  $(CXXFLAGS) uDaraChatServer.cpp  ChatLibrary.o GroupContainer.o uWebSockets/uSockets/*.o $(LIBS) -o uDaraChatServer

uDaraChatBot: uDaraChatBot.cpp ChatLibrary.o Chatter.o
	g++  $(CXXCLIENTFLAGS) uDaraChatBot.cpp  ChatLibrary.o Chatter.o $(LIBS) $(CLIENTLIBS) -o uDaraChatBot



ChatLibrary.o: ChatLibrary.cpp ChatLibrary.h
	g++  $(CXXFLAGS) -c ChatLibrary.cpp

GroupContainer.o: GroupContainer.cpp GroupContainer.h
	g++  $(CXXFLAGS) -c GroupContainer.cpp

Chatter.o: Chatter.cpp Chatter.h
	g++  $(CXXCLIENTFLAGS) -c Chatter.cpp
