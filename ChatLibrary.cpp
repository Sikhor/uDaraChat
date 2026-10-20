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

std::string ChatCmdTypeToString(EChatCmdType type) {
    switch (type) {
        case EChatCmdType::Tell: return "Tell";
        case EChatCmdType::Group: return "Group";
        case EChatCmdType::Raid: return "Raid";
        case EChatCmdType::Zone: return "Zone";
        case EChatCmdType::Channel: return "Channel";
        case EChatCmdType::GroupInvite: return "GroupInvite";
        case EChatCmdType::GroupJoin: return "GroupJoin";
        case EChatCmdType::GroupJoinInfo: return "GroupJoinInfo";
        case EChatCmdType::GroupDisband: return "GroupDisband";
        case EChatCmdType::GroupDisbandInfo: return "GroupDisbandInfo";
        case EChatCmdType::GroupKick: return "GroupKick";
        case EChatCmdType::GroupKickInfo: return "GroupKickInfo";
        case EChatCmdType::GroupInfo: return "GroupInfo";
        case EChatCmdType::Alive: return "Alive";
        case EChatCmdType::AliveInfo: return "AliveInfo";
        case EChatCmdType::AdminStatus: return "AdminStatus";
        case EChatCmdType::AdminGroups: return "AdminGroups";
        case EChatCmdType::AdminZones: return "AdminZones";
        case EChatCmdType::AdminChannels: return "AdminChannels";
        case EChatCmdType::AdminInfo: return "AdminInfo";
        case EChatCmdType::AdminError: return "AdminError";
        case EChatCmdType::None: return "None";
    }
    return "None";
}

EChatCmdType ChatCmdTypeFromString(const std::string& str) {
    if (str == "Tell") return EChatCmdType::Tell;
    if (str == "Group") return EChatCmdType::Group;
    if (str == "Raid") return EChatCmdType::Raid;
    if (str == "Zone") return EChatCmdType::Zone;
    if (str == "Channel") return EChatCmdType::Channel;
    if (str == "GroupInvite") return EChatCmdType::GroupInvite;
    if (str == "GroupJoin") return EChatCmdType::GroupJoin;
    if (str == "GroupJoinInfo") return EChatCmdType::GroupJoinInfo;
    if (str == "GroupDisband") return EChatCmdType::GroupDisband;
    if (str == "GroupDisbandInfo") return EChatCmdType::GroupDisbandInfo;
    if (str == "GroupKick") return EChatCmdType::GroupKick;
    if (str == "GroupKickInfo") return EChatCmdType::GroupKickInfo;
    if (str == "GroupInfo") return EChatCmdType::GroupInfo;
    if (str == "Alive") return EChatCmdType::Alive;
    if (str == "AliveInfo") return EChatCmdType::AliveInfo;
    if (str == "AdminStatus") return EChatCmdType::AdminStatus;
    if (str == "AdminGroups") return EChatCmdType::AdminGroups;
    if (str == "AdminZones") return EChatCmdType::AdminZones;
    if (str == "AdminChannels") return EChatCmdType::AdminChannels;
    if (str == "AdminInfo") return EChatCmdType::AdminInfo;
    if (str == "AdminError") return EChatCmdType::AdminError;
    if (str == "None") return EChatCmdType::None;
    return EChatCmdType::None;
}

std::string FDaraChatMsg::Serialize() const {
    return ChatCmdTypeToString(Type) + ":" + Sender + ":" + Recipient + ":" + Message;
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

bool uDaraChatLibrary::TryParseMessage(const std::string& input,
                                      FDaraChatMsg& output, std::string& error) {
    output = FDaraChatMsg{};
    error.clear();
    const auto first = input.find(':');
    const auto second = first == std::string::npos ? first : input.find(':', first + 1);
    const auto third = second == std::string::npos ? second : input.find(':', second + 1);
    if (third == std::string::npos) {
        error = "Expected Type:Sender:Recipient:Message";
        return false;
    }
    FDaraChatMsg parsed;
    parsed.Type = ChatCmdTypeFromString(input.substr(0, first));
    if (parsed.Type == EChatCmdType::None) {
        error = "Unknown or inactive message type";
        return false;
    }
    parsed.Sender = input.substr(first + 1, second - first - 1);
    parsed.Recipient = input.substr(second + 1, third - second - 1);
    parsed.Message = input.substr(third + 1);
    output = parsed;
    return true;
}

std::string uDaraChatLibrary::GetGroupJoinInfoMessage(FDaraChatMsg msg) {
    msg.Type = EChatCmdType::GroupJoinInfo;
    msg.Message = "joined the group";
    return msg.Serialize();
}

std::string uDaraChatLibrary::GetGroupDisbandInfoMessage(FDaraChatMsg msg) {
    msg.Type = EChatCmdType::GroupDisbandInfo;
    msg.Message = "left the group";
    return msg.Serialize();
}

std::string uDaraChatLibrary::GetGroupKickInfoMessage(FDaraChatMsg msg) {
    msg.Type = EChatCmdType::GroupKickInfo;
    return msg.Serialize();
}
