#include <uwebsockets/App.h>
#include <iostream>

int main() {
    uWS::App().ws<false>("/*", {
        .open = [](auto *ws) {
            std::cout << "Client connected\n";
        },
        .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
            ws->send(message, opCode);
        },
        .close = [](auto *ws, int /*code*/, std::string_view /*message*/) {
            std::cout << "Client disconnected\n";
        }
    }).listen(9020, [](auto *token) {
        if (token) {
            std::cout << "Server is listening on port 9020\n";
        }
    }).run();
}
