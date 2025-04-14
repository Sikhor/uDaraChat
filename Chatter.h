#pragma once
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
#include "ChatLibrary.h"




std::string GenerateHashedPassword(const std::string& passphrase);

class Chatter
{
public:

    int SilentMode=false;
    std::string MyCharName;
    std::string MyGroupId;
    std::string MyPass;
    std::string MyZoneName;
    int DebugLevel=0;

    Chatter();
    std::string GetConnectionString();
    std::string GetSubscribeString1();
    std::string GetSubscribeString2();
    std::string GetSubscribeString3();
    
    std::string GetRandomMessage(const std::vector<std::string>& messages);
    std::string GetRandomMessage();
    std::string GetRandomName() ;

    std::string GetRandomChatMsg();
    std::string PrepareChatMsg(FDaraChatMsg ChatMsg);

    void SendRandomMessage();
    std::string GetRandomInvite();
    void SendZoneMessage();
    void SendTellMessage();


    std::string GenerateRandomLogin();
private:
};
