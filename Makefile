all: uDaraChatServer uDaraChatBot

clean:
	rm -f uDaraChatServer uDaraChatBot *.o

.PHONY: LogClean
LogClean:
	@if [ -d log ]; then find log -type f -delete; fi
	@if [ -d log ]; then find log -mindepth 1 -maxdepth 1 -type d -name 'group-test-*' -exec rm -rf -- {} +; fi

run: uDaraChatServer uDaraChatBot
	@sh scripts/with-jwt-secret.sh ./uDaraChatServer


CXXFLAGS = -march=native -O3 -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++20 -I. -isystem third_party/jwt-cpp/include -IuWebSockets/src -IuWebSockets/uSockets/src -flto=auto
LIBS = -lz -lssl -lcrypto
CXXCLIENTFLAGS= -O0 -g -std=c++17 -I. 
CLIENTLIBS = -pthread

uDaraChatServer: uDaraChatServer.cpp JwtAuth.h ChatLibrary.o GroupContainer.o
	g++  $(CXXFLAGS) uDaraChatServer.cpp  ChatLibrary.o GroupContainer.o uWebSockets/uSockets/*.o $(LIBS) -o uDaraChatServer

uDaraChatBot: uDaraChatBot.cpp BotWebsocket.h ChatLibrary.o Chatter.o
	g++  $(CXXCLIENTFLAGS) uDaraChatBot.cpp  ChatLibrary.o Chatter.o $(LIBS) $(CLIENTLIBS) -o uDaraChatBot



ChatLibrary.o: ChatLibrary.cpp ChatLibrary.h
	g++  $(CXXFLAGS) -c ChatLibrary.cpp

GroupContainer.o: GroupContainer.cpp GroupContainer.h
	g++  $(CXXFLAGS) -c GroupContainer.cpp

Chatter.o: Chatter.cpp Chatter.h ChatLibrary.h
	g++  $(CXXCLIENTFLAGS) -c Chatter.cpp


.PHONY: StartServer StopServer CheckServer StartTest

StartTest: uDaraChatBot
	python3 tools/group_test_controller.py --unreal-character MeRobot

StopServer:
	@if pkill -TERM -x uDaraChatServer; then \
		echo "Sent termination signal to uDaraChatServer"; \
	else \
		status=$$?; \
		if [ "$$status" -eq 1 ]; then \
			echo "uDaraChatServer is NOT running"; \
		else \
			echo "Failed to stop uDaraChatServer" >&2; \
			exit "$$status"; \
		fi; \
	fi

StartServer: uDaraChatServer
	@if pgrep -x uDaraChatServer > /dev/null; then \
		echo "uDaraChatServer is already running"; \
		pgrep -x uDaraChatServer; \
	else \
		mkdir -p log; \
		sh scripts/with-jwt-secret.sh true || exit 1; \
		LOGFILE="log/chatserver-$$(date +%Y%m%d-%H%M%S).log"; \
		nohup sh scripts/with-jwt-secret.sh ./uDaraChatServer > "$$LOGFILE" 2>&1 & \
		echo "Started uDaraChatServer with PID $$!"; \
		echo "Logging to $$LOGFILE"; \
	fi

CheckServer:
	@if pgrep -x uDaraChatServer > /dev/null; then \
		echo "uDaraChatServer is RUNNING"; \
		pgrep -a -x uDaraChatServer; \
	else \
		echo "uDaraChatServer is NOT running"; \
	fi

.PHONY: test
test: build/JwtAuthTest build/ChatLibraryTest
	./build/JwtAuthTest
	./build/ChatLibraryTest

build/JwtAuthTest: tests/JwtAuthTest.cpp JwtAuth.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) tests/JwtAuthTest.cpp -lssl -lcrypto -o $@

.PHONY: test-controller
test-controller:
	python3 -m unittest discover -s tests -p 'test_group_controller.py' -v

.PHONY: test-bot-transport
test-bot-transport: build/BotTransportTest
	./build/BotTransportTest

build/BotTransportTest: tests/BotTransportTest.cpp BotWebsocket.h
	mkdir -p build
	$(CXX) $(CXXCLIENTFLAGS) tests/BotTransportTest.cpp -lssl -lcrypto -o $@

build/ChatLibraryTest: tests/ChatLibraryTest.cpp ChatLibrary.cpp ChatLibrary.h
	mkdir -p build
	$(CXX) $(CXXCLIENTFLAGS) tests/ChatLibraryTest.cpp ChatLibrary.cpp -o $@
