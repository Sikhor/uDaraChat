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
        case EChatType::GroupInvite: return "GroupInvite";
        case EChatType::GroupJoin: return "GroupJoin";
        case EChatType::GroupDisband: return "GroupDisband";
        case EChatType::Tell: return "Tell";
        case EChatType::Group: return "Group";
        case EChatType::World: return "World";
        case EChatType::Zone: return "Zone";
        case EChatType::Raid: return "Raid";
        case EChatType::Channel: return "Channel";
        default: return "Unknown";
    }
}



inline EChatType ChatTypeFromString(const std::string& str) {
    static const std::unordered_map<std::string, EChatType> stringToChatType = {
        {"GroupInvite", EChatType::GroupInvite},
        {"GroupJoin", EChatType::GroupJoin},
        {"GroupDisband", EChatType::GroupDisband},
        {"Tell", EChatType::Tell},
        {"Group", EChatType::Group},
        {"World", EChatType::World},
        {"Zone", EChatType::Zone},
        {"Raid", EChatType::Raid},
        {"Channel", EChatType::Channel}
    };

    auto it = stringToChatType.find(str);
    if (it != stringToChatType.end()) {
        return it->second;
    }

    throw std::invalid_argument("Invalid ChatType string: " + str);
}


std::string FDaraChatMsg::SerializeToSend()
{
    std::string serialized = ChatType + ":" +
                              Sender + ":" +
                              Recipient + ":" +
                              Msg;
    return serialized;
}

std::string FDaraChatMsg::SerializeToPost()
{
    std::string serialized = ChatType + ":" +
                              Sender + ":" +
                              Msg;
    return serialized;
}

std::string FDaraChatMsg::getTopic()
{
    if(ChatType=="Tell"){
        std::string topic = "tell_" + ToLower(Recipient);
        return topic;
    }
    if(ChatType=="Zone"){
        std::string topic = "zone_" + Recipient;
        return topic;
    }
    if(ChatType=="Group"){
        std::string topic = "group_";
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

    if (parts.size() < 2 ) {
        FDaraChatMsg emptyMsg;
        return emptyMsg; // Invalid message format      
    }

    FDaraChatMsg msg;
    msg.ChatType = parts[0];
    msg.Sender = parts[1];
    msg.Recipient= "";// its always me as I received it

    if(parts.size() < 3)    {
        msg.Msg = ""; // No message content
        return msg;
    } 
    // Join the remaining parts for the full message (in case message contains colons)
    msg.Msg = parts[2];
    for (size_t i = 3; i < parts.size(); ++i) {
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

    if (parts.size() < 3 ) {
        FDaraChatMsg emptyMsg;
        return emptyMsg; // Invalid message format      
    }

    FDaraChatMsg msg;
    msg.ChatType = parts[0];
    msg.Sender = parts[1];
    msg.Recipient = parts[2];

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




std::string uDaraChatLibrary::GetGroupJoinMessage(FDaraChatMsg msg)
{
    msg.ChatType= "GroupJoin";
    std::string message = msg.ChatType + ":" + msg.Sender + ":" + msg.Msg;
    return message;
}
std::string uDaraChatLibrary::GetGroupDisbandMessage(FDaraChatMsg msg)
{
    msg.ChatType= "GroupDisband";
    std::string message = msg.ChatType + ":" + msg.Sender + ":" + msg.Msg;
    return message;
}
std::string uDaraChatLibrary::GetGroupKickMessage(FDaraChatMsg msg)
{   
    msg.ChatType= "GroupKick";
    std::string message = msg.ChatType + ":" + msg.Sender + ":" + msg.Msg;
    return message;
}
