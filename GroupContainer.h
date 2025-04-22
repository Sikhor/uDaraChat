#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <ctime>
#include <unordered_set>



class GroupMember
{
    public:
        std::string name;
        bool isLeader=false;
        std::string groupId;
        std::string zone;
        std::string health;
        std::time_t storeTime; 
    GroupMember();
};

struct GroupInviteData
{
    std::string leaderName;
    std::string memberName;
    std::time_t storeTime; 
};


struct GroupInfo  
{
    std::string groupId;
    std::string leaderName;
    bool isLeader=false;
    int memberCount=0;
    int errorCode=0;
    std::vector<GroupMember> groupMembers;
    std::string SerializeToSend();
    GroupInfo Deserialize(const std::string msg);
};

class GroupContainer
{
    public:
        GroupContainer();
        ~GroupContainer();

        std::string MyGroupPrefix="group_";
        int MyMaxGroupMembers=6;
        int MyDebugLevel=0;

        GroupInfo GroupStartGroup(const std::string& leaderName); 
        GroupInfo GroupJoin(const std::string& memberName, const std::string& leaderName); 
        GroupInfo GroupInvite(const std::string& memberName, const std::string& leaderName); 
        GroupInfo GroupKickMember(const std::string& memberName, const std::string& leaderName);
        GroupInfo GroupDisband(const std::string& memberName);
        void AssignLeader(const std::string& groupId);
        void AliveMsg(const GroupMember &member);
        void RemoveLDGroupMembers();

        GroupInfo GetCurrentGroupId(const std::string& memberName);
        GroupInfo GetGroupInfo(const std::string& memberName);
        GroupInfo GetGroupInfoByGroupId(const std::string& groupId);

        std::string GetNewGroupId();

        void RunTests();
        void RunGroupTests();
        void RunGroupKickTests();
        void SetDebugLevel(int level);
        void DebugMsg(std::string msg);
        std::vector<std::string> GetUniqueGroupIds();
        void DumpGroups();
        void DumpInvites();
        int TestMsg(int msgId, std::string msg);
        void ErrorMsg(int msgId, std::string msg);

        std::string GetGroupTopic(std::string groupId);

        void RemoveOldInvites();
        void AddInvite(std::string leaderName, std::string memberName);
        void RemoveInvite(std::string leaderName, std::string memberName);
        bool CheckHasInvited(std::string leaderName, std::string memberName);

protected:



private:
    std::vector<GroupMember> MyGroups;
    std::vector<GroupInviteData> MyInvites;
};