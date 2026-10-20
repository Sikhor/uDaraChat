#define DARA_WEBSOCKET_TLS
#include "BotWebsocket.h"
#include <iostream>
#include <vector>

struct Handler {
    std::vector<int> received;
    template<class Connection>
    void onWSMsg(Connection&, uint8_t opcode, const uint8_t*, uint32_t) {
        received.push_back(opcode);
    }
};
struct TestConnection : websocket::WSConnection<Handler, char, false, 4096, true> {
    TestConnection() { init(0); }
    using websocket::WSConnection<Handler, char, false, 4096, true>::handleWSMsg;
};

int main() {
    websocket::SocketTcpConnection<4096> socket;
    if (!socket.write(nullptr, 0)) return 1;
    const uint8_t byte = 0;
    if (socket.write(&byte, 1)) return 2;

    Handler handler;
    TestConnection connection;
    // A text message and heartbeat arriving together must BOTH be dispatched.
    uint8_t together[] = {0x81, 1, 'x', 0x89, 0};
    if (connection.handleWSMsg(&handler, together, sizeof(together)) != 0) return 3;
    if (handler.received != std::vector<int>{websocket::OPCODE_TEXT, websocket::OPCODE_PING}) return 4;
    uint8_t partial[] = {0x81, 1, 'x', 0x89};
    if (connection.handleWSMsg(&handler, partial, sizeof(partial)) != 1) return 5;
    uint8_t shortHeader[] = {0x81, 126, 0};
    if (connection.handleWSMsg(&handler, shortHeader, sizeof(shortHeader)) != 3) return 6;
    uint8_t ping[] = {0x89, 0};
    if (connection.handleWSMsg(&handler, ping, sizeof(ping)) != 0) return 7;
    std::cout << "Transport checks passed: empty writes, coalesced heartbeat frames and partial headers.\n";
}
