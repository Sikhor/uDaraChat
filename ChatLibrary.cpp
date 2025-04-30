#include "ChatLibrary.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <algorithm>
#include <cstring>


#define DARAGROUPSUCCESS 1
#define DARAGROUPIDEMPTY -1
#define DARAGROUPISINANOTHERGROUP -2
#define DARAGROUPMEMBERALREADYINTHISGROUP -3
#define DARAGROUPISNOTLEADER -4
#define DARAGROUPISNOTMEMBER -5
#define DARAGROUPERRORFULL -6
#define DARAGROUPISNOTMEMBEROFANYGROUP -7
#define DARAGROUPISALREADYINGROUP -8
#define DARAEMPTYGROUP -9

#include <codecvt>
#include <locale>

std::wstring Utf8ToWString(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(str);
}

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


bool FDaraRawChatMsg::Deserialize(const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    // Check header
    if (data.size() < 4 || std::memcmp(&data[offset], "DARA", 4) != 0)
        return false;
    offset += 4;

    auto readFloat = [&](float& out) {
        if (offset + sizeof(float) > data.size()) return false;
        std::memcpy(&out, &data[offset], sizeof(float));
        offset += sizeof(float);
        return true;
    };

    auto readString = [&](std::string& out) {
        int32_t strLen = 0;
        if (offset + sizeof(int32_t) > data.size()) {
            std::cout <<"ERROR: String Size not ok"<<std::endl;
            return false;
        }
        std::memcpy(&strLen, &data[offset], sizeof(int32_t));
        offset += sizeof(int32_t);

        if (strLen < 0 || offset + static_cast<size_t>(strLen) * sizeof(wchar_t) > data.size()) {
                std::cout <<"ERROR: String either <0 or > data.size. strlen="<< std::to_string(strLen)<< std::endl;
                std::cout <<"ERROR: data.size="<< std::to_string(data.size())<< std::endl;
                std::cout <<"ERROR: offset="<< std::to_string(offset)<< std::endl;
                std::cout <<"ERROR: offset + static_cast<size_t>(strLen) * sizeof(wchar_t)="<< std::to_string(offset + static_cast<size_t>(strLen) * sizeof(wchar_t))<< std::endl;
            return false;
        }

        // FString uses UTF-16 or UTF-32 depending on platform, but in binary it's usually TCHAR = UTF-16.
        // Unreal on Windows = 2-byte TCHARs, but if you're on Linux, it's often 4-byte.
        // If you're using UTF-8 in Unreal, and serializing as FString, you may need to convert here.

        // Simplified: assume UTF-8, read raw as char*
        out.assign(reinterpret_cast<const char*>(&data[offset]), static_cast<size_t>(strLen));
        offset += static_cast<size_t>(strLen);
        return true;
    };
  
   
    // Vector
    if (!readFloat(Location.X) || !readFloat(Location.Y) || !readFloat(Location.Z)){
        std::cout <<"ERROR: Cannot read Location"<<std::endl;
        return false;
    } else{
        std::cout <<"Log: X="<< std::to_string(Location.X)<< std::endl;
        std::cout <<"Log: Y="<< std::to_string(Location.Y)<< std::endl;
        std::cout <<"Log: Z="<< std::to_string(Location.Z)<< std::endl;
        
    }

    // Stats
    if (!readFloat(Health)     || !readFloat(MaxHealth) ||
        !readFloat(Mana)       || !readFloat(MaxMana)   ||
        !readFloat(Energy)     || !readFloat(MaxEnergy)){
            std::cout <<"ERROR: Cannot read Stats"<<std::endl;
            return false;
        }else{
            std::cout <<"Log: Health="<< std::to_string(Health)<< std::endl;
            std::cout <<"Log: MaxHealth="<< std::to_string(MaxHealth)<< std::endl;
            std::cout <<"Log: Mana="<< std::to_string(Mana)<< std::endl;
            std::cout <<"Log: MaxMana="<< std::to_string(MaxMana)<< std::endl;
            std::cout <<"Log: Energy="<< std::to_string(Energy)<< std::endl;
            std::cout <<"Log: MaxEnergy="<< std::to_string(MaxEnergy)<< std::endl;            
        }

    // Strings
    
    if (!readString(Zone) || !readString(CharName)){
        std::cout <<"ERROR: Cannot read Strings"<<std::endl;
        return false;
    } 
    

    return true;
}

float SwapFloat(float value)
{
    union {
        float f;
        uint8_t b[4];
    } source, dest;

    source.f = value;
    dest.b[0] = source.b[3];
    dest.b[1] = source.b[2];
    dest.b[2] = source.b[1];
    dest.b[3] = source.b[0];

    return dest.f;
}

std::vector<uint8_t> FDaraRawChatMsg::Serialize() const
{
    std::vector<uint8_t> buffer;

    // 4-byte header
    const char header[4] = { 'D', 'A', 'R', 'A' };
    buffer.insert(buffer.end(), header, header + 4);

    auto writeFloat = [&](float value) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), ptr, ptr + sizeof(float));
    };

    auto writeFString = [&](const std::string& utf8Str) {
        // Convert std::string (UTF-8) to std::wstring (UTF-32)
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring wstr = converter.from_bytes(utf8Str);

        // Append null terminator as Unreal expects
        wstr.push_back(L'\0');

        int32_t length = static_cast<int32_t>(wstr.length());
        const uint8_t* lenPtr = reinterpret_cast<const uint8_t*>(&length);
        buffer.insert(buffer.end(), lenPtr, lenPtr + sizeof(int32_t));

        const uint8_t* strPtr = reinterpret_cast<const uint8_t*>(wstr.data());
        buffer.insert(buffer.end(), strPtr, strPtr + static_cast<size_t>(length) * sizeof(wchar_t));  // wchar_t = 4 bytes on Linux
    };

    // Serialize FVector
    writeFloat(Location.X);
    writeFloat(Location.Y);
    writeFloat(Location.Z);

    // Serialize stats
    writeFloat(Health);
    writeFloat(MaxHealth);
    writeFloat(Mana);
    writeFloat(MaxMana);
    writeFloat(Energy);
    writeFloat(MaxEnergy);

    // Serialize strings (Zone and CharName)
    //writeFString(Zone);
    //writeFString(CharName);

    return buffer;
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
