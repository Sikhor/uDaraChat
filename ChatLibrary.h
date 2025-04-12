#include <vector>
#include <string>
#include <map>
#include <iostream>

struct ChatMessage {
    std::string msgType;
    std::string sender;
    std::string receiver;
    std::string message;
};

struct GroupMember
{
    std::string name;
    bool isLeader=false;
    std::string groupId;
};

class uDaraChatLibrary
{
    public:
        uDaraChatLibrary();
        ~uDaraChatLibrary();

        std::string MyGroupPrefix="Group-";
        int MyMaxGroupMembers=6;
        int MyDebugLevel=0;

        ChatMessage ParseChatMessage(const std::string& input);

        int GroupJoin(const std::string& memberName, std::string& groupId); 
        int GroupInvite(const std::string& memberName, const std::string& groupId, const std::string& leaderName); 
        int GroupKickMember(const std::string& leaderName, const std::string& groupId, const std::string& memberName);
        int GroupDisband(const std::string& memberName);

        int GetCurrentGroupId(const std::string& leaderName, std::string& groupId);

        std::string GetNewGroupId();

        void RunTests();
        void SetDebugLevel(int level);
        void DebugMsg(std::string msg);
        void DumpGroups();
        int TestMsg(int msgId, std::string msg);
        void ErrorMsg(int msgId, std::string msg);

protected:



private:
    std::vector<GroupMember> MyGroups;

};