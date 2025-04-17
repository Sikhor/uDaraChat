#include <vector>
#include <string>
#include <map>
#include <iostream>

#define DARACHATSERVERADDRESS "85.215.47.227:9020"
#define DARACHATSERVERPORT 9020
#define DARACHATSERVERIP "85.215.47.227"


enum class EChatType {
    GroupInvite,
	GroupJoin,
	GroupDisband,
	Tell,
	Group,
	World,
	Zone,
	Raid,
	Channel,
};



class FDaraChatMsg {
public:
    std::string ChatType;
    std::string Sender;
    std::string Recipient;
    std::string Msg;


    std::string SerializeToSend();
    std::string SerializeToPost();
    std::string getTopic();
};


class uDaraChatLibrary
{
    public:
        uDaraChatLibrary();
        ~uDaraChatLibrary();

        int MyDebugLevel=0;

        FDaraChatMsg ParseSentMessage(const std::string& input);
        FDaraChatMsg ParseReceivedMessage(const std::string& input);
        std::string GetGroupJoinMessage(FDaraChatMsg msg);
        std::string GetGroupDisbandMessage(FDaraChatMsg msg);
        std::string GetGroupKickMessage(FDaraChatMsg msg);

        std::string GetNewGroupId();

        void SetDebugLevel(int level);
        void DebugMsg(std::string msg);
        void ErrorMsg(int msgId, std::string msg);

protected:



private:
};