#include "client/websocket.h"
#include "Chatter.h"
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <random>
#include <atomic>
#include <mutex>
#include <signal.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

#define SLEEPSECONDS 3
#define DEBUGMODE 1


Chatter chatter;
uDaraChatLibrary chatLibrary;


class Client
{
public:
  using WSClient = websocket::WSClient<Client>;
  using WSConn = WSClient::Connection;



  void run(std::string mode, std::string GivenName, std::string Recipient) {
    if (!wsclient.wsConnect(3000, DARACHATSERVERIP, DARACHATSERVERPORT, "/", DARACHATSERVERADDRESS)) {
      std::cout << "wsclient connect failed: " << wsclient.getLastError() << std::endl;
      return;
    }
    running = true;
    ws_thr = std::thread([this]() {
    while (running.load(std::memory_order_relaxed) && wsclient.isConnected()) {
      {
    	   std::lock_guard<std::mutex> lck(mtx);
     	   wsclient.poll(this);
    	   //std::cout << "Client polling..." << std::endl;
	   
   }
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
    	//std::cout << "Client wake up..." << std::endl;
        //std::this_thread::yield();
      }
    });

    std::cout << "Client running..." << std::endl;
    std::string line;

    if(GivenName.empty()){
      GivenName= chatter.GenerateRandomLogin();
    }else{
      chatter.SetCharName(GivenName);
    } 
    if(Recipient.empty()){
      chatter.AddChatPartner(chatter.GetRandomName());
    }else{
      chatter.AddChatPartner(Recipient);
    }
    
    // Login 
    std::string msg= chatter.GetConnectionString();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString1();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString2();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString3();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    
    
    

    while (running.load(std::memory_order_relaxed) && wsclient.isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(SLEEPSECONDS));
        ChatLoop(mode);    
    }
    stop();

    ws_thr.join();
    std::cout << "Client stopped..." << std::endl;
  }

  void stop() { running = false; }



  void onWSClose(WSConn& conn, uint16_t status_code, const char* reason) {
    std::cout << "ws close, status_code: " << status_code << ", reason: " << reason << std::endl;
  }

  void onWSMsg(WSConn& conn, uint8_t opcode, const uint8_t* payload, uint32_t pl_len) {
    if (opcode == websocket::OPCODE_PING) {
      conn.send(websocket::OPCODE_PONG, payload, pl_len);
      return;
    }
    if (opcode != websocket::OPCODE_TEXT) {
      std::cout << "got none text msg, opcode: " << (int)opcode << std::endl;
      return;
    }
    std::cout.write((const char*)payload, pl_len);
    std::cout << std::endl;
  }

  // no need to define onWSSegment if using c++17
  void onWSSegment(WSConn& conn, uint8_t opcode, const uint8_t* payload, uint32_t pl_len, uint32_t pl_start_idx,
                   bool fin) {
    std::cout << "error: onWSSegment should not be called" << std::endl;
  }


private:
  WSClient wsclient;
  std::mutex mtx;
  std::thread ws_thr;
  std::string admincmd_help;
  std::atomic<bool> running;

public:
  /*************************** Music plays here *******************/
  void ChatLoop(std::string mode)
  {
    static int count=0;
    count++;
    if(mode=="silent"){
      std::string msg= chatter.GetRandomChatMsg();
      msg= chatter.GetRandomZoneMsg();
      msg= chatter.GetRandomTellMsg();
      std::cout << msg <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }
    if(mode=="invite"){
      std::string msg= chatter.GetRandomInvite();
      std::cout << msg <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }
    if(mode=="schedule"){
      std::this_thread::sleep_for(std::chrono::seconds(SLEEPSECONDS));
      std::string msg= "schedule:";
      std::cout << msg << count <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }
  }


};


Client client;


void my_handler(int s) {
  client.stop();
}

int main(int argc, char** argv) {
  std::cout << "Client trying to connect" << std::endl;

  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);


  /* Parsing */
  if(argc<2){
    std::cout << "Help:" << std::endl;
    std::cout << "uDaraChatBot --mode <mode> --name <name>" << std::endl;
    std::cout << "   available modes:" << std::endl;
    std::cout << "   1.) silent - not output just in error case" << std::endl;
    std::cout << "   2.) world - random Messages to world" << std::endl;
    std::cout << "   3.) invitee - invitable chatter" << std::endl;
    return 1;
  }

  
  // Example: check for a --name argument
  std::string mode;
  std::string name;
  std::string recipient;
  for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--mode" && i + 1 < argc) {
          mode = argv[i + 1];
          ++i; // skip the next argument, already used as name
      }
      if (arg == "--name" && i + 1 < argc) {
        name = argv[i + 1];
        ++i; // skip the next argument, already used as name
      }
      if (arg == "--recipient" && i + 1 < argc) {
        recipient = argv[i + 1];
        ++i; // skip the next argument, already used as name
      }
}

  if (!mode.empty()) {
      std::cout << "Starting with mode " << name << "!" << "Name: " << name << std::endl;
  } else {
      std::cout << "Starting with standard mode." << "Name: " << name << std::endl;
  }
    


  std::cout << mode<< " mode activated." << std::endl;
  client.run(mode, name, recipient);
  return 0;
}
