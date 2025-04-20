#include "ChatLibrary.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <algorithm>


#define DARAGROUPSUCCESS 1
#define DARAGROUPIDEMPTY -1
#define DARAGROUPISINANOTHERGROUP -2
#define DARAGROUPMEMBERALREADYINTHISGROUP -3
#define DARAGROUPISNOTLEADER -4
#define DARAGROUPISNOTMEMBER -5
#define DARAGROUPERRORFULL -6
#define DARAGROUPISNOTMEMBEROFANYGROUP -7
#define DARAGROUPISALREADYINGROUP -8



std::string ToLower(const std::string& input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

inline std::string ChatTypeToString(EChatType type) {
    switch (type) {
        case EChatType::Channel: return "Channel";
        case EChatType::Cmd: return "Cmd";
        case EChatType::Group: return "Group";
        case EChatType::Raid: return "Raid";
        case EChatType::Tell: return "Tell";
        case EChatType::Zone: return "Zone";
        default: return "Unknown";
    }
}





inline EChatType ChatTypeFromString(const std::string& str) {
    static const std::unordered_map<std::string, EChatType> stringToChatType = {
        {"Channel", EChatType::Channel},
        {"Cmd", EChatType::Cmd},
        {"Group", EChatType::Group},
        {"Raid", EChatType::Raid},
        {"Group", EChatType::Group},
        {"Tell", EChatType::Tell},
        {"Zone", EChatType::Zone}
    };

    auto it = stringToChatType.find(str);
    if (it != stringToChatType.end()) {
        return it->second;
    }
    return EChatType::Unknown;
}


inline std::string ChatCmdTypeToString(EChatCmdType type) {
    switch (type) {
        case EChatCmdType::GroupInvite: return "GroupInvite";
        case EChatCmdType::GroupJoin: return "GroupJoin";
        case EChatCmdType::GroupJoinInfo: return "GroupJoinInfo";
        case EChatCmdType::GroupDisband: return "GroupDisband";
        case EChatCmdType::GroupDisbandInfo: return "GroupDisbandInfo";
        case EChatCmdType::GroupKick: return "GroupKick";
        case EChatCmdType::GroupKickInfo: return "GroupKickInfo";
        case EChatCmdType::GroupInfo: return "GroupInfo";
        case EChatCmdType::Alive: return "Alive";
        default: return "None";
    }   
}


inline EChatCmdType ChatCmdTypeFromString(const std::string& str) {
    static const std::unordered_map<std::string, EChatCmdType> stringToChatCmdType = {
        {"GroupInvite", EChatCmdType::GroupInvite},
        {"GroupJoin", EChatCmdType::GroupJoin},
        {"GroupJoinInfo", EChatCmdType::GroupJoinInfo},
        {"GroupDisband", EChatCmdType::GroupDisband},
        {"GroupDisbandInfo", EChatCmdType::GroupDisbandInfo},
        {"GroupKick", EChatCmdType::GroupKick},
        {"GroupKickInfo", EChatCmdType::GroupKickInfo},
        {"GroupInfo", EChatCmdType::GroupInfo},
        {"Alive", EChatCmdType::Alive},
        {"None", EChatCmdType::None}
    };

    auto it = stringToChatCmdType.find(str);
    if (it != stringToChatCmdType.end()) {
        return it->second;
    }
    return EChatCmdType::None;
}


FDaraChatMsg::FDaraChatMsg()
{
    ChatType = "Channel";
    ChatCmdType = "None";
    Sender = "";
    Recipient = "";
    Msg = "";
}

std::string FDaraChatMsg::SerializeToSend()
{
    std::string serialized = ChatType + ":" +
                              ChatCmdType + ":" +
                              Sender + ":" +
                              Recipient + ":" +
                              Msg;
    return serialized;
}

std::string FDaraChatMsg::SerializeToPost()
{
    std::string serialized = ChatType + ":" +
                              ChatCmdType + ":" + 
                              Sender + ":" +
                              Msg;
    return serialized;
}



std::string FDaraChatMsg::getTopicPrefix()
{
    if(ChatType=="Channel"){
        std::string topic = "channel_";
        return topic;
    }
    if(ChatType=="Cmd"){
        std::string topic = "tell_";
        return topic;
    }
    if(ChatType=="Group"){
        std::string topic = "group_";
        return topic;
    }
    if(ChatType=="Raid"){
        std::string topic = "raid_";
        return topic;
    }
    if(ChatType=="Tell"){
        std::string topic = "tell_";
        return topic;
    }
    if(ChatType=="Zone"){
        std::string topic = "zone_";
        return topic;
    }
    if(ChatType=="Unknown"){
        std::string topic = "";
        return topic;
    }
    return ChatType;
}


uDaraChatLibrary::uDaraChatLibrary() {}

uDaraChatLibrary::~uDaraChatLibrary() {}



std::string uDaraChatLibrary::GetNewGroupId()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11); // For UUID variant

    std::stringstream ss;
    ss << std::hex;

    for (int i = 0; i < 8; ++i)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; ++i)
        ss << dis(gen);
    ss << "-4"; // UUID version 4
    for (int i = 0; i < 3; ++i)
        ss << dis(gen);
    ss << "-";
    ss << dis2(gen); // UUID variant
    for (int i = 0; i < 3; ++i)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; ++i)
        ss << dis(gen);

    return ss.str();
}

void uDaraChatLibrary::SetDebugLevel(int level)
{
    // Set the debug level for logging
    MyDebugLevel = level;
}  




void uDaraChatLibrary::DebugMsg(std::string msg)
{
    if(MyDebugLevel>5){
        std::cout << msg << std::endl;
    }
}




void uDaraChatLibrary::ErrorMsg(int code, std::string msg)
{
    std::cout << "Error: " << code << " "<<msg << std::endl;
}

FDaraChatMsg uDaraChatLibrary::ParseReceivedMessage(const std::string& input)
{
    std::stringstream ss(input);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, ':')) {
        parts.push_back(segment);
    }

    if (parts.size() < 3 ) {
        FDaraChatMsg emptyMsg;
        return emptyMsg; // Invalid message format      
    }

    FDaraChatMsg msg;
    msg.ChatType = parts[0];
    msg.ChatCmdType = parts[1];
    msg.Sender = parts[2];
    msg.Recipient= "";// its always me as I received it

    if(parts.size() < 4)    {
        msg.Msg = ""; // No message content
        return msg;
    } 
    // Join the remaining parts for the full message (in case message contains colons)
    msg.Msg = parts[3];
    for (size_t i = 4; i < parts.size(); ++i) {
        msg.Msg += ":" + parts[i];
    }

    return msg;
}


FDaraChatMsg uDaraChatLibrary::ParseSentMessage(const std::string& input)
{
    std::stringstream ss(input);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, ':')) {
        parts.push_back(segment);
    }

    if (parts.size() < 4 ) {
        FDaraChatMsg emptyMsg;
        return emptyMsg; // Invalid message format      
    }

    FDaraChatMsg msg;
    msg.ChatType = parts[0];
    msg.ChatCmdType = parts[1];
    msg.Sender = parts[2];
    msg.Recipient = parts[3];

    if(parts.size() < 5)    {
        msg.Msg = ""; // No message content
        return msg;
    } 
    // Join the remaining parts for the full message (in case message contains colons)
    msg.Msg = parts[4];
    for (size_t i = 5; i < parts.size(); ++i) {
        msg.Msg += ":" + parts[i];
    }

    return msg;
}




std::string uDaraChatLibrary::GetGroupJoinInfoMessage(FDaraChatMsg msg)
{
    msg.ChatType= "Group";
    msg.ChatCmdType= "GroupJoinInfo";
    std::string message = msg.ChatType + ":" + msg.Sender + ":joined the group";
    return message;
}
std::string uDaraChatLibrary::GetGroupDisbandInfoMessage(FDaraChatMsg msg)
{
    msg.ChatType= "Group";
    msg.ChatCmdType= "GroupDisbandInfo";
    std::string message = msg.ChatType + ":" + msg.Sender + ":left the group";
    return message;
}
std::string uDaraChatLibrary::GetGroupKickInfoMessage(FDaraChatMsg msg)
{   
    msg.ChatType= "Group";
    msg.ChatCmdType= "GroupKickInfo";
    std::string message = msg.ChatType + ":" + msg.Sender + ":" + msg.Msg;
    return message;
}
