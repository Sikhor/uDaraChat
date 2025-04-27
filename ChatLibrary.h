#include <vector>
#include <string>
#include <map>
#include <iostream>

#define DARACHATSERVERADDRESS "85.215.47.227:9020"
#define DARACHATSERVERPORT 9020
#define DARACHATSERVERIP "85.215.47.227"

std::string ToLower(const std::string& input);


enum class EChatType {
	Channel,
    Cmd,
	Group,
	Raid,
    Tell,
	Zone,
    Unknown
};

enum class EChatCmdType{
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

inline std::string ChatCmdTypeToString(EChatCmdType type);
inline EChatCmdType ChatCmdTypeFromString(const std::string& str);  

inline std::string ChatTypeToString(EChatType type) ;
inline EChatType ChatTypeFromString(const std::string& str);


class FDaraChatMsg {
public:
    std::string ChatType;
    std::string ChatCmdType;
    std::string Sender;
    std::string Recipient;
    std::string Msg;

    FDaraChatMsg();
    std::string SerializeToSend();
    std::string SerializeToPost();
    std::string getTopicPrefix();
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

        FDaraChatMsg ParseSentMessage(const std::string& input);
        FDaraChatMsg ParseReceivedMessage(const std::string& input);
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