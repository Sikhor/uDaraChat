#include "websocket.h"
#include "DaraLibrary.h"
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


#define SLEEPSECONDS 10
#define DEBUGMODE 1

std::string GenerateHashedPassword(const std::string& passphrase)
{
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA-1 is 20 bytes

    SHA1(reinterpret_cast<const unsigned char*>(passphrase.c_str()), passphrase.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return oss.str();
}


class Client
{
public:
  using WSClient = websocket::WSClient<Client>;
  using WSConn = WSClient::Connection;

  int SilentMode=false;
  std::string MyCharName;
  std::string MyGroupId;
  std::string MyPass;
  std::string MyZoneName;

  void run(int mode) {
    SilentMode= mode;
    if (!wsclient.wsConnect(3000, "85.215.47.227", 9020, "/", "85.215.47.227:9020")) {
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

    // Login

    MyCharName= GetRandomName();
    MyZoneName= "Dungeon";
    MyGroupId= "AAAAAAMYGROUPIDAAAAAAAAAAAAAA";
    MyPass= GenerateHashedPassword(MyCharName);
    line="connect:name="+MyCharName+ ";zone=Alpha;group=7;pass="+MyPass;
    SendAMessage(line);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    SendAMessage("subscribe:World");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    SendAMessage("subscribe:" + MyCharName);
    SendAMessage("subscribe:" + MyZoneName);
    SendAMessage("subscribe:" + MyGroupId);
   

    while (running.load(std::memory_order_relaxed) && wsclient.isConnected()) {
      std::this_thread::sleep_for(std::chrono::seconds(SLEEPSECONDS));
      if(SilentMode==false){
        // std::cout << "Sending a message now"<<std::endl;
        SendRandomMessage();
        SendRandomInvite();
        SendZoneMessage();
        SendTellMessage();
      }
    }
    stop();

    ws_thr.join();
    std::cout << "Client stopped..." << std::endl;
  }

  void stop() { running = false; }

  void SendAMessage(std::string msg)
  {
      if(DEBUGMODE>9){
        std::cout << "Sending message: " << msg << std::endl;
      }

      std::lock_guard<std::mutex> lck(mtx);
	    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());

  }
  
  void SendRandomMessage()
  {
	  FDaraChatMsg ChatMsg;
	  ChatMsg.ChatType= "World";
	  ChatMsg.Sender= MyCharName;
	  ChatMsg.Recipient= "World";
	  ChatMsg.Msg= GetRandomMessage();
	  SendAChatMsg(ChatMsg);
  }

  void SendRandomInvite()
  {
    static int count=0;

    count++;
    if(count>10){
      count=0;
    }
    if(count==3){
      FDaraChatMsg ChatMsg;
	    ChatMsg.ChatType= "GroupInvite";
	    ChatMsg.Sender= MyCharName;
	    ChatMsg.Recipient= "MeMercenary";
	    ChatMsg.Msg= MyGroupId;
	    SendAChatMsg(ChatMsg);
    } 
    return;
  }

  void SendZoneMessage()
  {
    static int count=0;

    count++;
    if(count>10){
      count=0;
    }
    if(count==5){
      FDaraChatMsg ChatMsg;
	    ChatMsg.ChatType= "Zone";
	    ChatMsg.Sender= MyCharName;
	    ChatMsg.Recipient= MyZoneName;
	    ChatMsg.Msg= "Talking to Zone: "+GetRandomMessage();
	    SendAChatMsg(ChatMsg);
    } 
    return;
  }

  void SendTellMessage()
  {
    static int count=0;

    count++;
    if(count>5){
      count=0;
    }
    if(count==5){
      FDaraChatMsg ChatMsg;
	    ChatMsg.ChatType= "Tell";
	    ChatMsg.Sender= MyCharName;
	    ChatMsg.Recipient= "MeMercenary";
	    ChatMsg.Msg= GetRandomMessage();
	    SendAChatMsg(ChatMsg);
    } 
    return;
  }

  void SendAChatMsg(FDaraChatMsg ChatMsg)
  {
      std::string msg="publish:"+ ChatMsg.Recipient+ ":"+ ChatMsg.ChatType+ ":" + ChatMsg.Sender+":" + ChatMsg.Msg;
      if(DEBUGMODE>9){
        std::cout << MyCharName << ": " << ChatMsg.Msg << std::endl;
      }
      if(DEBUGMODE>0){
        std::cout << ChatMsg.Sender<<": " << ChatMsg.Msg << std::endl;
      }
      SendAMessage(msg);
  }


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

  std::string GetRandomMessage() {
	static std::vector<std::string> messages = {
		"Why me?",
		"Really?",
		"I just got here!",
		"I'm not paid enough for this.",
		"This isn't in my contract!",
		"Again?!",
		"Great. Just great.",
		"Is that thing supposed to glow?",
		"Nope. I'm out.",
		"What did I just step in?",
		"I think it's watching me...",
		"This planet gives me the creeps.",
		"Who's bright idea was this?",
		"I thought this was a vacation mission.",
		"I left the oven on back home.",
		"If I die, tell my clone I said hi.",
		"I demand hazard pay!",
		"Aliens again? Ugh.",
		"Not the face!",
		"I've got a bad feeling about this..."
	};
    // Random device and engine (static so they're initialized only once)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, messages.size() - 1);

    return messages[dist(gen)];
  }

   std::string GetRandomName() {
    static std::vector<std::string> messages = {
	    "AutoClient_X99",
	    "ChatProbe_007",
	    "NullEcho",
	    "ByteSniffer",
	    "PingLord",
	    "CaptainSpam",
	    "DaraBot9000",
	    "AFKCommander",
	    "SyntaxTerror",
	    "PacketJockey",
	    "WhisperWarden",
	    "MessageMancer",
	    "Talkonaut",
	    "CyberGoblin",
	    "EchoSpecter",
	    "NetGremlin",
	    "QueueHopper",
	    "Botzilla",
	    "IdleWhisper",
	    "GhostInTheChat"
	};

    // Random device and engine (static so they're initialized only once)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, messages.size() - 1);

    return messages[dist(gen)];
  }


private:
  WSClient wsclient;
  std::mutex mtx;
  std::thread ws_thr;
  std::string admincmd_help;
  std::atomic<bool> running;
};

Client client;

void my_handler(int s) {
  client.stop();
}

int main(int argc, char** argv) {
  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);

  if(argc>=2 && strcmp(argv[1], "silent")==0){
    std::cout << "Silent mode activated." << std::endl;
    client.run(true);
  }
  else{
    std::cout << "Chatty mode activated." << std::endl;
    client.run(false);
  }
  return 0;
}

