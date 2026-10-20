#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <iostream>

#define DARACHATSERVERADDRESS "85.215.47.227:9020"
#define DARACHATSERVERPORT 9020
#define DARACHATSERVERIP "85.215.47.227"

std::string ToLower(const std::string& input);


enum class EChatCmdType {
    Tell,
    Group,
    Raid,
    Zone,
    Channel,
    GroupInvite,
    GroupJoin,
    GroupJoinInfo,
    GroupDisband,
    GroupDisbandInfo,
    GroupKick,
    GroupKickInfo,
    GroupInfo,
    Alive,
    AliveInfo,
    None
};

std::string ChatCmdTypeToString(EChatCmdType type);
EChatCmdType ChatCmdTypeFromString(const std::string& str);

struct FDaraChatMsg {
    EChatCmdType Type = EChatCmdType::None;
    std::string Sender;
    std::string Recipient;
    std::string Message;

    std::string Serialize() const;
};

struct FVector {
    float X;
    float Y;
    float Z;
};

struct FDaraRawChatMsg {
    FVector Location;
    float Health;
    float MaxHealth;
    float Mana;
    float MaxMana;
    float Energy;
    float MaxEnergy;
    std::string Zone;
    std::string CharName;

    bool Deserialize(const std::vector<uint8_t>& data);
    std::vector<uint8_t> Serialize() const;
};


class uDaraChatLibrary
{
    public:
        uDaraChatLibrary();
        ~uDaraChatLibrary();

        int MyDebugLevel=0;

        bool TryParseMessage(const std::string& input, FDaraChatMsg& output, std::string& error);
        std::string GetGroupJoinInfoMessage(FDaraChatMsg msg);
        std::string GetGroupDisbandInfoMessage(FDaraChatMsg msg);
        std::string GetGroupKickInfoMessage(FDaraChatMsg msg);

        std::string GetNewGroupId();

        void SetDebugLevel(int level);
        void DebugMsg(std::string msg);
        void ErrorMsg(int msgId, std::string msg);

protected:



private:
};