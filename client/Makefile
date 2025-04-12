# Former lines to compile for better performance
# @g++ -O3 -std=c++17 server.cc DaraSocketServer.cc -o DaraChatServer -pthread


All: uDaraChatBot uDaraListener

clean:
	@rm -f uDaraChatBot uDaraListener
	@rm -f *.o

uDaraChatBot: uDaraClient.cc DaraLibrary.h
	@g++ -O0 -g -std=c++17 uDaraClient.cc -lssl -lcrypto -o uDaraChatBot -pthread

uDaraListener: uDaraListener.cc DaraLibrary.h
	@g++ -O0 -g -std=c++17 uDaraListener.cc -lssl -lcrypto -o uDaraListener -pthread

