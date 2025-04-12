#include "ChatLibrary.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

#define DARAGROUPSUCCESS 1
#define DARAGROUPIDEMPTY -1
#define DARAGROUPISINANOTHERGROUP -2
#define DARAGROUPMEMBERALREADYINTHISGROUP -3
#define DARAGROUPISNOTLEADER -4
#define DARAGROUPISNOTMEMBER -5
#define DARAGROUPERRORFULL -6
#define DARAGROUPISNOTMEMBEROFANYGROUP -7
#define DARAGROUPISALREADYINGROUP -8

uDaraChatLibrary::uDaraChatLibrary() {}

uDaraChatLibrary::~uDaraChatLibrary() {}

int uDaraChatLibrary::GroupJoin(const std::string& memberName, std::string& groupId)
{
    int memberCount = 0;
    std::string leaderName="";

    if (groupId.empty()){
        return TestMsg(DARAGROUPIDEMPTY, "");
    }

    // First, check if the member is already in the group and count group members
    for (const GroupMember& member : MyGroups)
    {
        if (member.groupId == groupId)
        {
            if (member.name == memberName)
            {
                // Member is already in this group
                return TestMsg(DARAGROUPMEMBERALREADYINTHISGROUP, memberName);
            }
            if(member.isLeader){
                leaderName=member.name;
            }
            memberCount++;
        }
        if(member.name==memberName && member.groupId!=groupId){
            // Member is already in another group
            return TestMsg(DARAGROUPISINANOTHERGROUP, memberName+" "+groupId);
        }
    }

    // If group is full, do not add
    if (memberCount >= MyMaxGroupMembers)
    {
        return TestMsg(DARAGROUPERRORFULL,groupId);
    }

    // Add new member to group
    GroupMember newMember;
    newMember.name = memberName;
    newMember.groupId = groupId;
    newMember.isLeader = false;
    MyGroups.push_back(newMember);
    return DARAGROUPSUCCESS;
}


// should only work if
// leaderName is in the groupId
// group is not full
// memberName is not already in the group
int uDaraChatLibrary::GroupInvite(const std::string& memberName, const std::string& groupId, const std::string& leaderName)
{
    std::string currentLeaderGroupId;
    std::string currentMemberGroupId;
    //DebugMsg("GroupInvite: "+leaderName+" Inviting "+memberName+" to group "+groupId);  

    GetCurrentGroupId(leaderName, currentLeaderGroupId);
    GetCurrentGroupId(memberName, currentMemberGroupId);
    if(currentLeaderGroupId != groupId){
        return TestMsg(DARAGROUPISNOTLEADER, "Group ID: "+groupId+" leader is not in that group. He is in: "+currentLeaderGroupId+", cannot invite member.");
    }
    if(!currentMemberGroupId.empty() && currentMemberGroupId != groupId){   
        return TestMsg(DARAGROUPISINANOTHERGROUP, "Member: "+memberName +" is already in another group, Group ID: "+currentMemberGroupId+" cannot invite.");
    }
    if(currentMemberGroupId == groupId){   
        return TestMsg(DARAGROUPISALREADYINGROUP, "Member: "+memberName);
    }

    return DARAGROUPSUCCESS;
}


int uDaraChatLibrary::GroupKickMember(const std::string& leaderName, const std::string& groupId, const std::string& memberName)
{
    bool found= false;
    std::string currentLeaderGroupId;
    // Check if the leader is in the group
    GetCurrentGroupId(leaderName, currentLeaderGroupId);
    if(currentLeaderGroupId != groupId){
        return TestMsg(DARAGROUPISNOTLEADER, "Group ID: "+groupId+" Group is not valid, cannot kick member.");
    }
    // Use remove-erase idiom to remove all matching members
    for (const GroupMember& member : MyGroups)
    {
        if (member.name == memberName && member.groupId== groupId)
        {
            found= true;
            break;
        }
    }

    if(found){
        MyGroups.erase(
            std::remove_if(MyGroups.begin(), MyGroups.end(),
                [&](const GroupMember& member)
                {
                    return member.name == memberName && member.groupId == groupId;
                }),
            MyGroups.end()
        );
    }
    return DARAGROUPSUCCESS;
}

// always returns true;
int uDaraChatLibrary::GroupDisband(const std::string& memberName)
{
    // Use remove-erase idiom to remove all matching members
    MyGroups.erase(
        std::remove_if(MyGroups.begin(), MyGroups.end(),
            [&](const GroupMember& member)
            {
                return member.name == memberName;
            }),
        MyGroups.end()
    );
    return DARAGROUPSUCCESS;
}

int uDaraChatLibrary::GetCurrentGroupId(const std::string& memberName, std::string& groupId)
{
    bool groupValid= false;
    for (const GroupMember& member : MyGroups)
    {
        if (member.name == memberName)
        {
            // Group already exists
            groupId= member.groupId;
            groupValid= true;
            break;
        }
    }

    if(groupValid==true){
        return DARAGROUPSUCCESS;
    } else{
        return DARAGROUPISNOTMEMBEROFANYGROUP; 
    }
}




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

    return MyGroupPrefix + ss.str();
}

void uDaraChatLibrary::SetDebugLevel(int level)
{
    // Set the debug level for logging
    MyDebugLevel = level;
}  


void uDaraChatLibrary::DumpGroups()
{
    if (MyDebugLevel<10){
        return ;
    }
    std::cout << "Current Groups:" << std::endl;
    for (const GroupMember& member : MyGroups)
    {
        std::cout << "Group ID: " << member.groupId << " Name: " << member.name << ", Leader: " << (member.isLeader ? "Yes" : "No") << std::endl;
    }
}   


void uDaraChatLibrary::DebugMsg(std::string msg)
{
    if(MyDebugLevel>5){
        std::cout << msg << std::endl;
    }
}


void uDaraChatLibrary::RunTests()
{

    std::string DummyGoupId="";
    std::string FullGoupId="";
    std::string EmptyGoupId="";
    
    FullGoupId= GetNewGroupId();
    GroupJoin("Tornado", FullGoupId);
    GroupJoin("Eddy", FullGoupId);
    GroupJoin("Tirio", FullGoupId);
    GroupJoin("Blasti", FullGoupId);
    GroupJoin("Ticky", FullGoupId);
    GroupJoin("Tornie", FullGoupId);

    DummyGoupId= GetNewGroupId();
    GroupJoin("Bernie", DummyGoupId);
    GroupJoin("Hanna", DummyGoupId);
    GroupKickMember("Bernie", DummyGoupId, "Hanna");
    DumpGroups();

    GroupJoin("Hanna", DummyGoupId);
    GroupJoin("Porky", DummyGoupId);
    GroupJoin("Panny", DummyGoupId);
    GroupDisband("Hanna");
    GroupDisband("Porky");
    GroupDisband("Panny");
    DumpGroups();

    GroupJoin("Tenso", DummyGoupId);
    GroupInvite("Tynny", DummyGoupId, "Tenso");
    DumpGroups();

}

void uDaraChatLibrary::ErrorMsg(int code, std::string msg)
{
    std::cout << "Error: " << code << " "<<msg << std::endl;
}

int uDaraChatLibrary::TestMsg(int code, std::string msg)
{
    if(code==DARAGROUPIDEMPTY){
        std::cout << "Test: Group ID is empty. " <<msg << std::endl;
        return DARAGROUPIDEMPTY;
    }
    if(code==DARAGROUPISINANOTHERGROUP){
        std::cout << "Test: Member is already in another group." <<msg << std::endl;
        return DARAGROUPISINANOTHERGROUP;
    }
    if(code==DARAGROUPMEMBERALREADYINTHISGROUP){
        std::cout << "Test: Member is already in this group." <<msg << std::endl;
        return DARAGROUPMEMBERALREADYINTHISGROUP;
    }
    if(code==DARAGROUPISNOTLEADER){
        std::cout << "Test: Member is not the leader." <<msg << std::endl;
        return DARAGROUPISNOTLEADER;
    }
    if(code==DARAGROUPISNOTMEMBER){
        std::cout << "Test: Member is not in the group." <<msg << std::endl;
        return DARAGROUPISNOTMEMBER;
    }
    if(code==DARAGROUPERRORFULL){
        std::cout << "Test: Group is full." <<msg << std::endl;
        return DARAGROUPERRORFULL;
    }    
    if(code==DARAGROUPISNOTMEMBEROFANYGROUP){
        std::cout << "Test: Member is not in any group." <<msg << std::endl;
        return DARAGROUPISNOTMEMBEROFANYGROUP;
    }  
    if(code==DARAGROUPISALREADYINGROUP){
        std::cout << "Test: Member is already in the group." <<msg << std::endl;
        return DARAGROUPISALREADYINGROUP;
    }
    return DARAGROUPSUCCESS;
}


ChatMessage uDaraChatLibrary::ParseChatMessage(const std::string& input)
{
    std::stringstream ss(input);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, ':')) {
        parts.push_back(segment);
    }

    if (parts.size() < 5 || parts[0] != "publish") {
        ChatMessage emptyMsg;
        return emptyMsg; // Invalid message format      
    }

    ChatMessage msg;
    msg.receiver = parts[1];
    msg.msgType = parts[2];
    msg.sender = parts[3];

    // Join the remaining parts for the full message (in case message contains colons)
    msg.message = parts[4];
    for (size_t i = 5; i < parts.size(); ++i) {
        msg.message += ":" + parts[i];
    }

    return msg;
}

