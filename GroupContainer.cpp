#include "GroupContainer.h"
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
#define DARAGROUPMEMBERGROUPEMPTY -9
#define DARAGROUPLEADEREMPTY -10
#define DARAGROUPMEMBERALREADYINGROUP -11
#define DARAGROUPLEADERHASNOGROUP -12
#define DARAGROUPCANNOTKICKSELF -13





GroupMember::GroupMember() {
    isLeader=false;
    storeTime= std::time(nullptr);
}

std::string GroupInfo::SerializeToSend()
{
    std::string serialized = groupId + ";" +
                              leaderName + ";" +
                              std::to_string(isLeader) + ";" +
                              std::to_string(memberCount) + ";" +
                              std::to_string(errorCode);
    for (const auto& member : groupMembers) {
        serialized += ";" + member;
    }
    std::cout << "Serialized GroupInfo: " << serialized << std::endl;
    return serialized;
}

GroupInfo GroupInfo::Deserialize(const std::string msg)
{
    GroupInfo groupInfo;
    std::stringstream ss(msg);
    std::string segment;
    std::vector<std::string> parts;

    while (std::getline(ss, segment, ';')) {
        parts.push_back(segment);
    }

    if (parts.size() < 5) {
        return groupInfo; // Invalid message format
    }

    groupInfo.groupId = parts[0];
    groupInfo.leaderName = parts[1];
    groupInfo.isLeader = (parts[2] == "1");
    groupInfo.memberCount = std::stoi(parts[3]);
    groupInfo.errorCode = std::stoi(parts[4]);

    for (size_t i = 5; i < parts.size(); ++i) {
        groupInfo.groupMembers.push_back(parts[i]);
    }

    return groupInfo;
}

GroupContainer::GroupContainer() {}

GroupContainer::~GroupContainer() {}


// you should only be able to start a group if you are not already in a group
// but we only check if the groupId is empty
GroupInfo GroupContainer::GroupStartGroup(const std::string& leaderName)
{
    GroupInfo RetInfo;
    RetInfo= GetGroupInfo(leaderName);
    if(!RetInfo.groupId.empty()){
        RetInfo.errorCode= DARAGROUPMEMBERALREADYINGROUP;
        return RetInfo;
    }
    // Add the leader to the new group
    GroupMember newMember;
    newMember.name = leaderName;
    newMember.groupId = GetNewGroupId();
    newMember.isLeader = true;
    MyGroups.push_back(newMember);

    GroupInfo groupInfo;
    groupInfo.groupId = newMember.groupId;
    groupInfo.leaderName = newMember.name;
    groupInfo.isLeader = newMember.isLeader;
    groupInfo.memberCount = 1;
    groupInfo.errorCode = 0;

    return groupInfo;
}   

GroupInfo GroupContainer::GroupJoin(const std::string& memberName, const std::string& leaderName)
{
    GroupInfo LeaderInfo;
    GroupInfo MemberInfo;

    if (leaderName.empty()){
        LeaderInfo.errorCode= DARAGROUPLEADEREMPTY;
        return LeaderInfo;
    }

    LeaderInfo= GetGroupInfo(leaderName);
    if(LeaderInfo.groupId.empty()){
        LeaderInfo.errorCode= DARAGROUPLEADERHASNOGROUP    ;
        return LeaderInfo;
    }
    // If group is full, do not add
    if (LeaderInfo.memberCount >= MyMaxGroupMembers)
    {
        LeaderInfo.groupId="";
        LeaderInfo.errorCode= DARAGROUPERRORFULL;
        return LeaderInfo;
    }

    MemberInfo= GetGroupInfo(memberName);
    if(!MemberInfo.groupId.empty()){
        MemberInfo.errorCode= DARAGROUPMEMBERALREADYINGROUP;
        return MemberInfo;
    }


    // Add new member to group
    GroupMember newMember;
    newMember.name = memberName;
    newMember.groupId = LeaderInfo.groupId;
    newMember.isLeader = false;
    MyGroups.push_back(newMember);

    // as we join group group is now one more
    LeaderInfo.memberCount = LeaderInfo.memberCount + 1;

    return LeaderInfo;
}


// should only work if
// leaderName is in the groupId
// group is not full
// memberName is not already in the group
GroupInfo GroupContainer::GroupInvite(const std::string& memberName, const std::string& leaderName)
{
    GroupInfo LeaderInfo;
    GroupInfo MemberInfo;

    LeaderInfo= GetGroupInfo(leaderName);
    MemberInfo= GetGroupInfo(memberName);

    if(LeaderInfo.isLeader!=true){
        LeaderInfo.groupId="";
        LeaderInfo.memberCount=0;
        LeaderInfo.errorCode= DARAGROUPISNOTLEADER;
        return LeaderInfo;
    }
    if(MemberInfo.groupId!=""){   
        MemberInfo.errorCode= DARAGROUPISINANOTHERGROUP;
        MemberInfo.groupId="";
        MemberInfo.memberCount=0;
        return MemberInfo;
    }
    if(MemberInfo.groupId==LeaderInfo.groupId){   
        MemberInfo.errorCode= DARAGROUPISALREADYINGROUP;
        MemberInfo.groupId="";
        MemberInfo.memberCount=0;

        return MemberInfo;
    }

    LeaderInfo.errorCode= DARAGROUPSUCCESS;
    return LeaderInfo;
}


GroupInfo GroupContainer::GroupKickMember(const std::string& memberName, const std::string& leaderName)
{
    GroupInfo LeaderInfo;
    GroupInfo MemberInfo;
    if(memberName==leaderName){
        LeaderInfo.errorCode= DARAGROUPCANNOTKICKSELF;
        return LeaderInfo;
    }

    LeaderInfo= GetGroupInfo(leaderName);
    std::cout << "Leader: " << leaderName<< " Group: " << LeaderInfo.groupId << " Leader: " << LeaderInfo.isLeader<< std::endl;
    if(LeaderInfo.isLeader!=true ){
        LeaderInfo.errorCode= DARAGROUPISNOTLEADER;
        LeaderInfo.groupId="";
        LeaderInfo.memberCount=0; 
        return LeaderInfo;  
    }

    MemberInfo= GetGroupInfo(memberName);
    if(MemberInfo.groupId!=LeaderInfo.groupId){
        MemberInfo.errorCode= DARAGROUPISNOTMEMBER;
        return MemberInfo;
    }

    MyGroups.erase(
            std::remove_if(MyGroups.begin(), MyGroups.end(),
                [&](const GroupMember& member)
                {
                    return member.name == memberName;
                }),
            MyGroups.end()
        );

    GroupInfo RetInfo;
    return RetInfo;
}

// always returns true;
GroupInfo GroupContainer::GroupDisband(const std::string& memberName)
{
    GroupInfo MemberInfo;
    MemberInfo= GetGroupInfo(memberName);
    if(!MemberInfo.groupId.empty()){
        // Leaving this old group and need to inform group members
    }
    // Use remove-erase idiom to remove all matching members
    MyGroups.erase(
        std::remove_if(MyGroups.begin(), MyGroups.end(),
            [&](const GroupMember& member)
            {
                return member.name == memberName;
            }),
        MyGroups.end()
    );

    MemberInfo.errorCode= DARAGROUPSUCCESS;
    MemberInfo.groupId= "";
    MemberInfo.memberCount=0;
    MemberInfo.leaderName= "";
    return MemberInfo;
}


void GroupContainer::AliveMsg(const std::string& memberName)
{
    // Update the store time of the member
    for (GroupMember& member : MyGroups)
    {
        if (member.name == memberName)
        {
            member.storeTime = std::time(nullptr);
            break;
        }
    }
}

void GroupContainer::RemoveLDGroupMembers()
{
    std::time_t currentTime = std::time(nullptr);
    MyGroups.erase(
        std::remove_if(MyGroups.begin(), MyGroups.end(),
            [currentTime](const GroupMember& groupMember) {
                return std::difftime(currentTime, groupMember.storeTime) > 120;
            }),
        MyGroups.end());


}


// Get the current group ID for a member
GroupInfo GroupContainer::GetCurrentGroupId(const std::string& memberName)
{
    GroupInfo RetInfo;
    RetInfo= GetGroupInfo(memberName);

    return RetInfo;
}




std::string GroupContainer::GetNewGroupId()
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

void GroupContainer::SetDebugLevel(int level)
{
    // Set the debug level for logging
    MyDebugLevel = level;
}  

// returns membercount, the groupId, leaderName and memberCount
// if the member is not in a group, returns 0
GroupInfo GroupContainer::GetGroupInfo(const std::string& memberName)
{
    GroupInfo groupInfo;
    int memberCount=0;

    for (const GroupMember& member : MyGroups)
    {
        if (member.name == memberName)
        {
            groupInfo.groupId= member.groupId;
            break;
        }
    }

    if (groupInfo.groupId.empty()){
        groupInfo.errorCode= DARAGROUPISNOTMEMBEROFANYGROUP; 
        return groupInfo;
    }

    for (const GroupMember& member : MyGroups)
    {
        if (member.groupId == groupInfo.groupId)
        {
            groupInfo.memberCount= groupInfo.memberCount+1;
            groupInfo.groupMembers.push_back(member.name);
            memberCount++;
            //std::cout << "Counting: " << groupInfo.memberCount<< std::endl;
        }
        if(member.groupId == groupInfo.groupId && member.isLeader){
            groupInfo.leaderName=member.name;
            groupInfo.isLeader=true;
        }   
    }
    // std::cout << "MemberCount Sum: " << groupInfo.memberCount<< std::endl;
    groupInfo.memberCount=memberCount;

    return groupInfo;
}

GroupInfo GroupContainer::GetGroupInfoByGroupId(const std::string& groupId)
{
    GroupInfo groupInfo;
    int memberCount=0;

    groupInfo.groupId=groupId;

    for (const GroupMember& member : MyGroups)
    {
        if (member.groupId == groupId)
        {
            groupInfo.memberCount= groupInfo.memberCount+1;
            groupInfo.groupMembers.push_back(member.name);
            memberCount++;
            //std::cout << "Counting: " << groupInfo.memberCount<< std::endl;
        }
        if(member.groupId == groupId && member.isLeader){
            groupInfo.leaderName=member.name;
            groupInfo.isLeader=true;
        }   
    }
    //std::cout << "Counting Sum: " << groupInfo.memberCount<< std::endl;
    groupInfo.memberCount=memberCount;

    return groupInfo;
}



void GroupContainer::DumpGroups()
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

std::vector<std::string> GroupContainer::GetUniqueGroupIds()
{
    std::unordered_set<std::string> uniqueGroupIds;

    for (const auto& item : MyGroups){
        // Assuming item.groupId is a string
        // Check if the groupId is not empty before inserting
        if (!item.groupId.empty()){
            uniqueGroupIds.insert(item.groupId); // assuming groupId is std::string
        }
    }

    // Convert set to vector
    return std::vector<std::string>(uniqueGroupIds.begin(), uniqueGroupIds.end());
}

void GroupContainer::DumpInvites()
{
    if (MyDebugLevel<10){
        return ;
    }
    std::cout << "Current Invites:" << std::endl;
    for (const GroupInviteData& invite : MyInvites)
    {
        std::cout << "Invite: Leader: " << invite.leaderName << " Member: " << invite.memberName << std::endl;
    }
}   


void GroupContainer::DebugMsg(std::string msg)
{
    if(MyDebugLevel>5){
        std::cout << msg << std::endl;
    }
}


void GroupContainer::RunTests()
{
    GroupStartGroup("Tirio");
    GroupJoin("Trula", "Tirio");

    
    RunGroupTests();
    RunGroupKickTests();
    DumpGroups();
    return;

    GroupInfo groupInfo;
    std::string DummyGroup= "";

    groupInfo= GroupStartGroup("Member11");
    GroupJoin("Member12", "Member11");
    
    DummyGroup="";
    groupInfo= GroupStartGroup("Member21");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member22", "Member21");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member23", "Member22");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member24", "Member22");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member25", "Member22");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member26", "Member22");
    std::cout << groupInfo.memberCount << std::endl;
    groupInfo= GroupJoin("Member27", "Member22");
    std::cout << groupInfo.memberCount << std::endl;

    std::string memberName;

    memberName="Member12";
    groupInfo= GetGroupInfo(memberName);
    std::cout << "Member: " << memberName<< " Leader: "<< groupInfo.leaderName <<" #"<< groupInfo.memberCount << std::endl;
    memberName="Member26";
    groupInfo= GetGroupInfo(memberName);
    std::cout << "Member: " << memberName<< " Leader: "<< groupInfo.leaderName <<" #"<< groupInfo.memberCount << std::endl;
    memberName="Member27";
    groupInfo= GetGroupInfo(memberName);
    std::cout << "Member: " << memberName<< " Leader: "<< groupInfo.leaderName <<" #"<< groupInfo.memberCount << std::endl;

    DumpGroups();

}

void GroupContainer::ErrorMsg(int code, std::string msg)
{
    std::cout << "Error: " << code << " "<<msg << std::endl;
}

int GroupContainer::TestMsg(int code, std::string msg)
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

// Tests
void GroupContainer::RunGroupTests() {
    std::cout << "=== Starting Group Tests ===" << std::endl;

    // 1. Start a group
    auto groupInfo = GroupStartGroup("Leader1");
    std::cout << "Started group with Leader1, count: " << groupInfo.memberCount << std::endl;

    // 2. Join members up to the limit
    for (int i = 2; i <= 6; ++i) {
        std::string member = "Member" + std::to_string(i);
        groupInfo = GroupJoin(member, "Leader1");
        std::cout << member << " joined Leader1's group. New count: " << groupInfo.memberCount << std::endl;
    }

    // 3. Try joining a 7th member (should fail)
    auto overflow = GroupJoin("Member7", "Leader1");
    if (overflow.errorCode != DARAGROUPSUCCESS) {
        std::cout << "Correctly rejected 7th member: Member7. Group is full." << std::endl;
    } else {
        std::cerr << "❌ Error: Group allowed more than 6 members!" << std::endl;
    }

    // 4. Try duplicate entry
    auto duplicate = GroupJoin("Member3", "Leader1");
    if (duplicate.errorCode!= DARAGROUPSUCCESS) {
        std::cout << "Correctly rejected duplicate join for Member3." << std::endl;
    } else {
        std::cerr << "❌ Error: Duplicate Member3 joined again!" << std::endl;
    }

    // 5. Try joining a non-existent leader
    auto noGroup = GroupJoin("StrayMember", "UnknownLeader");
    if (noGroup.errorCode!= DARAGROUPSUCCESS) {
        std::cout << "Correctly handled join to non-existent group." << std::endl;
    } else {
        std::cerr << "❌ Error: Joined non-existent group!" << std::endl;
    }

    // 6. Start another group with someone already in a group
    auto secondStart = GroupStartGroup("Member4");
    if (secondStart.errorCode != DARAGROUPSUCCESS) {
        std::cout << "Correctly rejected group start by existing member (Member4)." << std::endl;
    } else {
        std::cerr << "❌ Error: Member4 started a second group while already in one!" << std::endl;
    }

    std::cout << "=== Group Tests Finished ===" << std::endl;
}


void GroupContainer::RunGroupKickTests() {
    std::cout << "=== Running GroupKickMember Tests ===" << std::endl;

    // 1. Setup: Start a group and add members
    auto startInfo = GroupStartGroup("LeaderKick");
    if (startInfo.errorCode != 0) {
        std::cerr << "❌ Failed to start group with LeaderKick. Error code: " << startInfo.errorCode << std::endl;
        return;
    }

    std::vector<std::string> members = {"Kickee1", "Kickee2", "Kickee3", "Kickee4"};
    for (const auto& member : members) {
        auto joinInfo = GroupJoin(member, "LeaderKick");
        if (joinInfo.errorCode != 0) {
            std::cerr << "❌ Failed to add member " << member << ". Error code: " << joinInfo.errorCode << std::endl;
        }
    }

    auto group = GroupJoin("Kickee5", "LeaderKick");
    std::cout << "Group count before kick: " << group.memberCount << std::endl;


    // 2. Kick a valid member
    group = GroupContainer::GroupKickMember("Kickee2", "LeaderKick");
    if (group.errorCode == 0) {
        std::cout << "✅ Successfully kicked Kickee2. New count: " << group.memberCount << std::endl;
    } else {
        std::cerr << "❌ Failed to kick Kickee2. Error code: " << group.errorCode << std::endl;
    }


    // 3. Try kicking same member again
    auto result = GroupContainer::GroupKickMember("Kickee2", "LeaderKick");
    if (result.errorCode != 0) {
        std::cout << "✅ Correctly rejected second kick of Kickee2. Error code: " << result.errorCode << std::endl;
    } else {
        std::cerr << "❌ Kickee2 was kicked again — should have already been removed!" << std::endl;
    }

    // 4. Try kicking a non-member
    auto kickNonMember = GroupContainer::GroupKickMember("RandomGuy", "LeaderKick");
    if (kickNonMember.errorCode != 0) {
        std::cout << "✅ Correctly handled non-member kick attempt." << std::endl;
    } else {
        std::cerr << "❌ RandomGuy was kicked — but wasn't in the group!" << std::endl;
    }

    // 5. Try kicking the leader
    auto kickLeader = GroupContainer::GroupKickMember("LeaderKick", "LeaderKick");
    if (kickLeader.errorCode != 0) {
        std::cout << "✅ Correctly blocked leader from being kicked." << std::endl;
    } else {
        std::cerr << "❌ LeaderKick was kicked from their own group!" << std::endl;
    }


    // 6. Try kicking from a non-existent group
    auto kickFromGhost = GroupContainer::GroupKickMember("Nobody", "GhostLeader");
    if (kickFromGhost.errorCode != 0) {
        std::cout << "✅ Correctly handled kick from non-existent group (GhostLeader)." << std::endl;
    } else {
        std::cerr << "❌ Kick from non-existent group unexpectedly succeeded!" << std::endl;
    }

    

    // 7. Member trying to kick Leader
    auto memberKickLeader = GroupContainer::GroupKickMember("LeaderKick", "Kickee3");
    if (memberKickLeader.errorCode != 0) {
        std::cout << "✅ Correctly handled member kick leader attempt." << std::endl;
    } else {
        std::cerr << "❌ Leader was kicked — but from a non leader!" << std::endl;
    }
    std::cout << "=== GroupKickMember Tests Finished ===" << std::endl;

    DumpGroups();
    return;
}



void GroupContainer::RemoveOldInvites() 
{
    int maxAgeSeconds= 180; // 3 minutes
    
    std::time_t now = std::time(nullptr);

    MyInvites.erase(std::remove_if(MyInvites.begin(), MyInvites.end(),
        [now, maxAgeSeconds](const GroupInviteData& entry) {
            return (now - entry.storeTime) > maxAgeSeconds;
        }), MyInvites.end());
}

void GroupContainer::RemoveInvite(std::string leaderName, std::string memberName)
{
    MyInvites.erase(
        std::remove_if(MyInvites.begin(), MyInvites.end(),
            [&](const GroupInviteData& invite)
            {
                return invite.leaderName == leaderName && invite.memberName == memberName;
            }),
        MyInvites.end()
    );
}

bool GroupContainer::CheckHasInvited(std::string leaderName, std::string memberName)
{
    for (const GroupInviteData& invite : MyInvites)
    {
        if (invite.leaderName == leaderName && invite.memberName == memberName)
        {
            return true;
        }
    }
    return false;
}   

void GroupContainer::AddInvite(std::string leaderName, std::string memberName)
{
    GroupInviteData invite;
    invite.leaderName = leaderName;
    invite.memberName = memberName;
    invite.storeTime = std::time(nullptr);
    MyInvites.push_back(invite);
}


std::string GroupContainer::GetGroupTopic(std::string groupId)
{
    std::string topic= "group_";
    topic+= groupId;
    return topic;   
}