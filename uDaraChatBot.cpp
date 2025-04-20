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
    std::string recMsg= std::string((const char*)payload, pl_len);

    //std::cout.write((const char*)payload, pl_len);
    std::cout <<"Received:"<<recMsg<< std::endl;
    FDaraChatMsg ChatMsg= chatLibrary.ParseReceivedMessage(recMsg);
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
  void ChatReply(FDaraChatMsg ChatMsg)
  {
      if(ChatMsg.ChatCmdType=="GroupInvite"){
        std::cout << "GroupInvite: " << ChatMsg.Sender << " Inviting " << ChatMsg.Recipient << " me to group " << ChatMsg.Msg << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Cmd";
        replyMsg.ChatCmdType= "GroupJoin";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= ChatMsg.Msg;
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.ChatType=="Tell" && ChatMsg.Msg.rfind("InviteMe", 0) == 0){
        std::cout << "Tell: " << ChatMsg.Sender << " requesting a group Invite " << ChatMsg.Recipient << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Cmd";
        replyMsg.ChatCmdType= "GroupInvite";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= "";
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.ChatType=="Tell" && ChatMsg.Msg.rfind("Ping", 0) == 0){
        std::cout << "Tell with Ping: " << ChatMsg.Sender << ":"  << ChatMsg.Msg << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Tell";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= "Pong";
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.ChatType=="Tell" && ChatMsg.Msg.rfind("Hello", 0) == 0){
        std::cout << "Tell with Hello: " << ChatMsg.Sender << ":"  << ChatMsg.Msg << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Tell";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= "Hi my friend! Good to hear from you!";
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.ChatType=="Tell" && ChatMsg.Msg.rfind("Pong", 0) != 0){
        std::cout << "Tell with Hello: " << ChatMsg.Sender << ":"  << ChatMsg.Msg << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Tell";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= "Hello my friend! Good to hear from you!";
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
        return;
      }
      if(ChatMsg.ChatType=="Cmd" && ChatMsg.ChatCmdType=="GroupInfo"  && ChatMsg.Msg.rfind("Are you there?", 0) == 0){
        std::cout << "Group: " << ChatMsg.Sender << " requesting a alive msg from me " << ChatMsg.Recipient << std::endl; 
        
        FDaraChatMsg replyMsg;
        replyMsg.ChatType= "Group";
        replyMsg.ChatCmdType= "None";
        replyMsg.Sender= chatter.MyCharName;
        replyMsg.Recipient= ChatMsg.Sender;
        replyMsg.Msg= "I am alive in group!";
        std::string msgStr= replyMsg.SerializeToSend();
        std::cout << msgStr <<std::endl;
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msgStr.data(), msgStr.size());
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
    if(mode=="silent"){
    }
    if(mode=="grouptests"){
      if(count==2){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupDisband";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupJoin";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="schedule"){
      std::this_thread::sleep_for(std::chrono::seconds(SLEEPSECONDS));
      std::string msg= "schedule:";
      std::cout << msg << count <<std::endl;
      wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
    }
    if(mode=="groupmember"){
      if(count==1){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupDisband";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==4){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupJoin";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "Group";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= "Group";
        ChatMsg.Msg= "I am in your group now " + std::to_string(upTimeCounter);
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==10){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupDisband";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= "Group";
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="groupleader"){
      if(count==2){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "GroupInvite";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
      }
      if(count==5){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "Group";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= "Group";
        ChatMsg.Msg= "Welcome to my group "+std::to_string(upTimeCounter);
        std::string msg= ChatMsg.SerializeToSend();
        wsclient.send(websocket::OPCODE_TEXT, (const uint8_t*)msg.data(), msg.size());
        std::cout << msg <<std::endl;
        count=0;
      }
    }
    if(mode=="pingpong"){
      std::string msg; 
      if(count==4){
        FDaraChatMsg ChatMsg;
        ChatMsg.ChatType= "Tell";
        ChatMsg.Sender= chatter.MyCharName;
        ChatMsg.Recipient= chatter.GetRandomChatPartner();
        ChatMsg.Msg= "Ping "+ std::to_string(upTimeCounter);
        msg= ChatMsg.SerializeToSend();
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
    std::cout << "uDaraChatBot --mode <mode> --name <name> --recipient <recipient>" << std::endl;
    std::cout << "   available modes:" << std::endl;
    std::cout << "   1.) silent - do not send messages" << std::endl;
    std::cout << "   2.) chatting - random Messages to world and everywhere" << std::endl;
    std::cout << "   3.) inviting - inviting a character" << std::endl;
    std::cout << "   4.) schedule - triggering schedules on server" << std::endl;
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

  if (!mode.empty() && mode != "silent" && mode != "chatting" && mode != "inviting" && mode != "schedule" && mode != "groupmember" && mode != "groupleader" && mode != "pingpong"&& mode != "grouptests") {
      std::cout << "Invalid mode. Available modes: silent, chatting, inviting, schedule, groupmember, groupleader, pingpong" << std::endl;
      return 1;
  }
  if (!mode.empty()) {
      std::cout << "Starting with mode " << mode << "!" << std::endl;
  } else {
      mode="silent";
      std::cout << "Starting with standard mode: "<< mode << std::endl;
  }
  if(!name.empty()) {
      std::cout << "Starting with name " << name << "!" << std::endl;
  } else {
      std::cout << "Starting with random name."<< std::endl;
      name= "";
  }
    


  client.run(mode, name, recipient);
  return 0;
}
