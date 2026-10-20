#define DARA_WEBSOCKET_TLS
#include "BotWebsocket.h"
#include <cstdlib>
#include <algorithm>
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
#include <poll.h>

#define SLEEPSECONDS 3000

constexpr const char* BotServerHost = "gameinfo.daraempire.com";
constexpr const char* BotServerAddress = "gameinfo.daraempire.com:9020";
#define DEBUGMODE 1


Chatter chatter;
uDaraChatLibrary chatLibrary;


class Client
{
public:
  using WSClient = websocket::WSClient<Client>;
  using WSConn = WSClient::Connection;



  void run(std::string mode, std::string GivenName, std::string Recipient, int SleepSeconds, const std::string& token) {
    if (!wsclient.wsConnect(3000, BotServerHost, DARACHATSERVERPORT, "/", BotServerAddress)) {
      std::cerr << "Connection to wss://" << BotServerAddress << "/ failed:\n  "
                << wsclient.getLastError()
                << "\nJWT authentication has not started; this failure occurred while opening the secure WebSocket."
                << std::endl;
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

    chatter.SetCharName(GivenName);
    if(Recipient.empty()){
      chatter.AddChatPartner(chatter.GetRandomName());
    }else{
      chatter.AddChatPartner(Recipient);
    }

    // Login and subscriptions share the polling mutex.
    {
    std::lock_guard<std::mutex> lock(mtx);
    std::string msg = "connect:name=" + GivenName + ";zone=" + chatter.MyZoneName + ";group=;pass=" + token;
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString1();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString2();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    msg= chatter.GetSubscribeString3();
    wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }




    if (mode == "controlled") std::cout << "CONTROL READY" << std::endl;
    while (running.load(std::memory_order_relaxed) && wsclient.isConnected()) {
        if (mode == "controlled") {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&input, 1, 100);
            if (ready > 0 && (input.revents & (POLLIN | POLLHUP))) {
                std::string command;
                if (!std::getline(std::cin, command)) { stop(); break; }
                std::lock_guard<std::mutex> lock(mtx);
                if (wsclient.isConnected()) ControlCommand(command);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(SleepSeconds));
            std::lock_guard<std::mutex> lock(mtx);
            if (wsclient.isConnected()) ChatLoop(mode);
        }
    }
    stop();

    ws_thr.join();
    std::cout << "Client stopped..." << std::endl;
  }

  void stop() { running = false; }



  void onWSClose(WSConn& conn, uint16_t status_code, const char* reason) {
    std::cerr << "WebSocket disconnected (code " << status_code << "): " << reason << std::endl;
    if (status_code == 1008) {
      std::cerr << "Server policy/authentication rejection: check token signature, expiry, issuer, audience and exact character name. Check server logs for the rejection reason." << std::endl;
    }
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
    std::string recMsg= std::string((const char*)payload, pl_len);

    //std::cout.write((const char*)payload, pl_len);
    std::cout <<"Received:"<<recMsg<< std::endl;
    FDaraChatMsg ChatMsg;
    std::string parseError;
    if (!chatLibrary.TryParseMessage(recMsg, ChatMsg, parseError)) return;
    ChatReply(ChatMsg);
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
  void ControlCommand(const std::string& command) {
      std::istringstream input(command);
      std::string action, target;
      input >> action >> target;
      if (action == "quit") { stop(); return; }
      FDaraChatMsg message;

      message.Sender = chatter.MyCharName;
      message.Recipient = target;
      if (action == "admin-status") message.Type = EChatCmdType::AdminStatus;
      else if (action == "admin-groups") message.Type = EChatCmdType::AdminGroups;
      else if (action == "admin-zones") message.Type = EChatCmdType::AdminZones;
      else if (action == "admin-channels") message.Type = EChatCmdType::AdminChannels;
      else if (action == "world" || action == "group") {
          const auto text = command.find_first_not_of(" ", command.find(' ') == std::string::npos ? command.size() : command.find(' '));
          if (text == std::string::npos) { std::cerr << action << " requires a message" << std::endl; return; }
          message.Type = action == "world" ? EChatCmdType::Channel : EChatCmdType::Group;

          message.Recipient = action == "world" ? "World" : "";
          message.Message = command.substr(text);
      }
      else if (action == "invite" && !target.empty()) message.Type = EChatCmdType::GroupInvite;
      else if (action == "kick" && !target.empty()) message.Type = EChatCmdType::GroupKick;
      else if (action == "leave") { message.Type = EChatCmdType::GroupDisband; message.Recipient.clear(); }
      else { std::cerr << "Control command: world <message>, group <message>, invite <name>, kick <name>, leave, admin-status, admin-groups, admin-zones, admin-channels, quit" << std::endl; return; }
      const std::string frame = message.Serialize();
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)frame.data(), frame.size());
      std::cout << "CONTROL SENT " << action << " " << target << std::endl;
  }

  void ChatReply(FDaraChatMsg ChatMsg)
  {
      if(ChatMsg.Type == EChatCmdType::GroupInvite){
        std::cout << "GroupInvite: " << ChatMsg.Sender << " Inviting " << ChatMsg.Recipient << " me to group " << ChatMsg.Message << std::endl;

        FDaraChatMsg replyMsg;

        replyMsg.Type = EChatCmdType::GroupJoin;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Message= ChatMsg.Message;
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.Type == EChatCmdType::Tell && ChatMsg.Message.rfind("InviteMe", 0) == 0){
        std::cout << "Tell: " << ChatMsg.Sender << " requesting a group Invite " << ChatMsg.Recipient << std::endl;

        FDaraChatMsg replyMsg;

        replyMsg.Type = EChatCmdType::GroupInvite;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Message= "";
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.Type == EChatCmdType::Tell && ChatMsg.Message.rfind("Ping", 0) == 0){
        std::cout << "Tell with Ping: " << ChatMsg.Sender << ":"  << ChatMsg.Message << std::endl;

        FDaraChatMsg replyMsg;
        replyMsg.Type = EChatCmdType::Tell;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Message= "Pong";
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.Type == EChatCmdType::Tell && ChatMsg.Message.rfind("Hello", 0) == 0){
        std::cout << "Tell with Hello: " << ChatMsg.Sender << ":"  << ChatMsg.Message << std::endl;

        FDaraChatMsg replyMsg;
        replyMsg.Type = EChatCmdType::Tell;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Message= "Hi my friend! Good to hear from you!";
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.Type == EChatCmdType::Tell && ChatMsg.Message.rfind("Pong", 0) != 0){
        std::cout << "Tell with Hello: " << ChatMsg.Sender << ":"  << ChatMsg.Message << std::endl;

        FDaraChatMsg replyMsg;
        replyMsg.Type = EChatCmdType::Tell;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Message= "Hello my friend! Good to hear from you!";
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.Type == EChatCmdType::GroupInfo){
        std::cout << "Group: " << ChatMsg.Sender << " requesting a alive msg from me " << ChatMsg.Recipient << std::endl;

        /* old method with string
        FDaraChatMsg replyMsg;

        replyMsg.Type = EChatCmdType::Alive;
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient = "";
        replyMsg.Message= chatter.MyHealth;
        std::string msgStr= replyMsg.Serialize();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        */
        FDaraRawChatMsg msg;
        msg.CharName = chatter.MyCharName;
        msg.Zone = chatter.MyZoneName;
        msg.Location = { 100.0f, 200.0f, 0.0f };
        msg.Health = 90.f;
        msg.MaxHealth = 100.f;
        msg.Mana = 50.f;
        msg.MaxMana = 75.f;
        msg.Energy = 30.f;
        msg.MaxEnergy = 50.f;

        std::cout << "Linux Bot Message:\n";
        std::cout << "Location: (" << msg.Location.X << ", " << msg.Location.Y << ", " << msg.Location.Z << ")\n";
        std::cout << "Health: " << msg.Health << "\n";
        std::cout << "MaxHealth: " << msg.MaxHealth << "\n";
        std::cout << "Mana: " << msg.Mana << "\n";
        std::cout << "MaxMana: " << msg.MaxMana << "\n";
        std::cout << "Energy: " << msg.Energy << "\n";
        std::cout << "MaxEnergy: " << msg.MaxEnergy << "\n";

        std::vector<uint8_t> payload = msg.Serialize();
        wsclient.send(websocket::OPCODE_BINARY, (const uint8_t*)payload.data(), payload.size());

        return;
      }
  }
  /*************************** Music plays here *******************/
  void ChatLoop(std::string mode)
  {
    static int count=0;
    static int upTimeCounter=0;
    count++;
    upTimeCounter++;
    if(mode=="chatting"){
      std::string msg;
      if(count==1){
        msg= chatter.GetRandomChatMsg();
      }
      if(count==2){
        msg= chatter.GetRandomZoneMsg();
      }
      if(count==3){
        msg= chatter.GetRandomTellMsg();
      }
      if(count==4){
        msg= chatter.GetRandomGroupMsg();
        count= 0;
      }
      std::cout << msg <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());

    }
    if(mode=="inviting"){
      std::string msg= chatter.GetRandomInvite();
      std::cout << msg <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }
    if(mode=="Binary"){
      FDaraRawChatMsg msg;
        msg.CharName = chatter.MyCharName;
        msg.Zone = chatter.MyZoneName;
        msg.Location = { 100.0f, 200.0f, 0.0f };
        msg.Health = 90.f;
        msg.MaxHealth = 91.f;
        msg.Mana = 92.f;
        msg.MaxMana = 93.f;
        msg.Energy = 94.f;
        msg.MaxEnergy = 95.f;

        std::cout << "Linux Bot Message:\n";
        std::cout << "Location: (" << msg.Location.X << ", " << msg.Location.Y << ", " << msg.Location.Z << ")\n";
        std::cout << "Health: " << msg.Health << "\n";
        std::cout << "MaxHealth: " << msg.MaxHealth << "\n";
        std::cout << "Mana: " << msg.Mana << "\n";
        std::cout << "MaxMana: " << msg.MaxMana << "\n";
        std::cout << "Energy: " << msg.Energy << "\n";
        std::cout << "MaxEnergy: " << msg.MaxEnergy << "\n";
        std::cout << "Zone: " << msg.Zone << "\n";
        std::cout << "CharName: " << msg.CharName << "\n";

        std::vector<uint8_t> payload = msg.Serialize();
        wsclient.send(websocket::OPCODE_BINARY, (const uint8_t*)payload.data(), payload.size());

    }
    if(mode=="silent"){
    }
    if(mode=="grouptests"){
      if(count==2){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupDisband;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient = "";
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupJoin;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="schedule"){
      std::string msg= "schedule:";
      std::cout << msg << count <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
      std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    if(mode=="groupmember"){
      if(count==1){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupDisband;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient = "";
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==4){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupJoin;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::Group;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient = "";
        ChatMsg.Message= "I am in your group now " + std::to_string(upTimeCounter);
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==10){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupDisband;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient = "";
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="groupleader"){
      if(count==2){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::GroupInvite;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::Group;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient = "";
        ChatMsg.Message= "Welcome to my group "+std::to_string(upTimeCounter);
        std::string msg= ChatMsg.Serialize();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="pingpong"){
      std::string msg;
      if(count==4){
        FDaraChatMsg ChatMsg;
        ChatMsg.Type = EChatCmdType::Tell;
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        ChatMsg.Message= "Ping "+ std::to_string(upTimeCounter);
        msg= ChatMsg.Serialize();
        std::cout << "PingPong: "<< msg <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        count=0;
      }
    }

  }


};


Client client;


void my_handler(int s) {
  client.stop();
}


void SyntaxFailure()
{
  std::cout << "Help:" << std::endl;
    std::cout << "uDaraChatBot --mode <mode> --name <JWT characterName> [--token <JWT>] --recipient <recipient> --sleep <seconds>" << std::endl;
    std::cout << "   Use --token <JWT> or set DARA_CHAT_JWT_TOKEN; --token takes precedence." << std::endl;
    std::cout << "   controlled - accept chat, invite/kick/leave, admin-status/admin-groups/admin-zones/admin-channels and quit commands on stdin" << std::endl;
    std::cout << "   available modes:" << std::endl;
    std::cout << "   1.) silent - do not send messages" << std::endl;
    std::cout << "   2.) chatting - random Messages to world and everywhere" << std::endl;
    std::cout << "   3.) inviting - inviting a character" << std::endl;
    std::cout << "   4.) schedule - triggering schedules on server" << std::endl;
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
    SyntaxFailure();
    return 1;
  }


  // Example: check for a --name argument
  std::string mode;
  std::string name;
  std::string recipient;
  std::string token;
  bool tokenProvided = false;
  int SleepSeconds = SLEEPSECONDS;


  for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--token") {
        if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0) {
          std::cerr << "--token requires a JWT value." << std::endl;
          return 1;
        }
        token = argv[++i];
        tokenProvided = true;
      }
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
      if (arg == "--sleep" && i + 1 < argc) {
        SleepSeconds = atoi(argv[i + 1]);
        ++i; // skip the next argument, already used as name
      }
}

    if(mode.empty()){
        SyntaxFailure();
        return 1;
    }

  if (!mode.empty() && mode != "controlled" && mode != "silent" && mode != "chatting" && mode != "inviting" && mode != "schedule" && mode != "groupmember" && mode != "groupleader" && mode != "pingpong"&& mode != "grouptests") {
      std::cout << "Invalid mode. Available modes: silent, chatting, inviting, schedule, groupmember, groupleader, pingpong" << std::endl;
      return 1;
  }
  if (!mode.empty()) {
      std::cout << "Starting with mode " << mode << "!" << std::endl;
  } else {
      mode="silent";
      std::cout << "Starting with standard mode: "<< mode << std::endl;
  }
  const char* configuredToken = std::getenv("DARA_CHAT_JWT_TOKEN");
  if (!tokenProvided && configuredToken) token = configuredToken;
  if (name.empty() || name.find_first_of(";\r\n") != std::string::npos ||
      token.empty()) {
      std::cerr << "A valid --name matching JWT characterName and a JWT via --token or DARA_CHAT_JWT_TOKEN are required." << std::endl;
      return 1;
  }
  if (token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ .") != std::string::npos ||
      token.find(' ') != std::string::npos || std::count(token.begin(), token.end(), '.') != 2) {
      std::cerr << "Token must be a compact JWT." << std::endl;
      return 1;
  }
  if(!name.empty()) {
      std::cout << "Starting with name " << name << "!" << std::endl;
  } else {
      std::cout << "Starting with random name."<< std::endl;
      name= "";
  }


  std::cout << "Starting with Sleep  " << SleepSeconds << " Seconds." << std::endl;


  client.run(mode, name, recipient, SleepSeconds, token);
  return 0;
}
