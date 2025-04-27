#include "App.h"
#include "ChatLibrary.h"
#include "GroupContainer.h"


#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

#include <arpa/inet.h>      // For inet_ntop
#include <netinet/in.h>     // For sockaddr_in and sockaddr_in6
#include <cstring>          // For memset if needed

   
#define DEBUGLEVEL 1

uDaraChatLibrary *chatLibrary;
GroupContainer *groupContainer;


struct us_listen_socket_t *global_listen_socket;

struct TopicSlot{
    std::string topic;
    bool used=false;
};

/* ws->getUserData returns one of these */
struct PerSocketData {
    /* Fill with user data */
    std::string name;
    std::string zone;
    std::string groupId;
    std::vector<TopicSlot> topics;
    int nr = 0;

    bool connected = false;
};


std::string GenerateHashedPassword(const std::string& passphrase)
{
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA-1 is 20 bytes

    SHA1(reinterpret_cast<const unsigned char*>(passphrase.c_str()), passphrase.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return oss.str();
}


std::string ConvertMappedIPv6ToIPv4(const std::string& ipv6)
{
    // Assume the format is always "0000:...:ffff:xxxx:yyyy"
    size_t lastColon = ipv6.rfind(':');
    size_t secondLastColon = ipv6.rfind(':', lastColon - 1);

    if (lastColon == std::string::npos || secondLastColon == std::string::npos)
        return "";

    std::string hex1 = ipv6.substr(secondLastColon + 1, lastColon - secondLastColon - 1);
    std::string hex2 = ipv6.substr(lastColon + 1);

    int h1 = std::stoi(hex1, nullptr, 16);
    int h2 = std::stoi(hex2, nullptr, 16);

    // Extract 2 bytes from each part
    int a = (h1 >> 8) & 0xFF;
    int b = h1 & 0xFF;
    int c = (h2 >> 8) & 0xFF;
    int d = h2 & 0xFF;

    std::ostringstream oss;
    oss << a << "." << b << "." << c << "." << d;
    return oss.str();
}

bool HandleConnect(PerSocketData *perSocketData, const std::string& Msg, const std::string &ipAddress) 
{
    std::string msg = Msg.substr(8); // remove "connect:"
            
    perSocketData->connected = false;

    // Expected format: name=John;zone=Alpha;group=7
    std::istringstream ss(msg);
    std::string token;
    while (std::getline(ss, token, ';')) {
        auto delimiterPos = token.find('=');
        if (delimiterPos != std::string::npos) {
            std::string key = token.substr(0, delimiterPos);
            std::string value = token.substr(delimiterPos + 1);

            if (key == "name") perSocketData->name = value;
            else if (key == "zone") perSocketData->zone = value;
            // ignoring group... cannot be passed in else if (key == "group") perSocketData->groupId = value;
            else if (key == "pass") {
                // Check if the password is correct
                std::string hashedPassword = GenerateHashedPassword(perSocketData->name);
                if (hashedPassword == value) { // Replace with your expected hashed password
                    perSocketData->connected = true;
                    perSocketData->name = perSocketData->name;
                } else {
                    std::cout << "ERROR: "<<ipAddress << "Invalid password for user:" << perSocketData->name << std::endl;
                    perSocketData->connected = false;
                    return false;
                }
            }
        }
    }
    if(perSocketData->connected==false){
        std::cout <<"ERROR: "<<ipAddress << "Invalid connection attempt from user:" << perSocketData->name << std::endl;
        return false;
    }

    std::cout << "Log: "<<ipAddress << " [Client connected] name:" << perSocketData->name
            << ", zone: " << perSocketData->zone << std::endl;

    perSocketData->connected = true;
    return true;
}

bool DumpTopicSubscription(PerSocketData *data) 
{
    std::cout << "Dumping topic subscriptions for: " << data->name << std::endl;
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];
        if (slot.used) {
            std::cout << "Topic slot " << i << ":" << slot.topic << std::endl;
        }
    }
    return true;
}



bool RemoveGroupSubscription(uWS::WebSocket<true, true, PerSocketData>* ws) 
{
    PerSocketData *data = (PerSocketData *) ws->getUserData();
    if(data->groupId.empty()){
        std::cout << "No groupId set, so no need to unsubscribe" << std::endl;
        return false;
    }
    std::string oldGroupTopic;
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];

        if (slot.used && slot.topic.rfind("group_", 0) == 0) {
            oldGroupTopic = slot.topic;
            data->topics[i].topic= "none";
            data->topics[i].used= false;
            std::cout << data->name << " Removed group topic in slot " << i << ":" << oldGroupTopic << std::endl;
            ws->unsubscribe(oldGroupTopic);
            return true;
        }
    }
    std::cout << "Old group topic slot not found so no need to unsusbcribe" << std::endl;
    return false;
}

bool RemoveTopicSubscription(uWS::WebSocket<true, true, PerSocketData>* ws,PerSocketData *data, const std::string& oldTopic) 
{
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];

        if (slot.used && slot.topic == oldTopic) {
            data->topics[i].topic= "none";
            data->topics[i].used= false;
            std::cout << data->name << " Removed topic in slot " << i << ":" << oldTopic << std::endl;
            ws->unsubscribe(oldTopic);
            return true;
        }
    }
    std::cout << "Old topic slot not found so no need to unsusbcribe: " << oldTopic << std::endl;
    return false;
}


bool StoreTopicSubscription(PerSocketData *data, const std::string& newTopic) 
{
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];

        if (!slot.used) {
            slot.topic = newTopic;
            slot.used = true;

            data->topics[i].topic= newTopic;
            data->topics[i].used= true;
            std::cout << data->name << " Subscribed to new topic in slot " << i << ":" << newTopic << std::endl;
            return true;
        }
        if(slot.topic == newTopic && slot.used==true) {
            std::cout << data->name << " Already Subscribed to topic in slot " << i << ":" << newTopic << std::endl;
            return true;
        }
    }
    std::cout << "No available topic slots to subscribe to: " << newTopic << std::endl;
    return false;
}

bool CheckValidTopicName(const std::string& topicName) {
    // tell channels are only for direct tells. Therefore they are forbidden
    if (topicName.rfind("tell_", 0) == 0) {
        std::cout << "Invalid topic reserved for a private tell channel: " << topicName << std::endl;
        return false;
    }
    // zone channels are only for the zone server. Therefore they are forbidden
    if (topicName.rfind("zone_", 0) == 0) {
        std::cout << "Invalid topic reserved for a zone channel: " << topicName << std::endl;
        return false;
    }
    // Check if the topic name is valid (e.g., not empty, no special characters)
    if (topicName.empty() || topicName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) {
        return false;
    }
    return true;
}

void HandleSubscribeMessage(uWS::WebSocket<true, true, PerSocketData>* ws, std::string msg) {
    PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();

    const std::string prefix = "subscribe:";
    if (msg.rfind(prefix, 0) != 0) return;
    std::string newTopic = msg.substr(prefix.length());
    if(!CheckValidTopicName(newTopic)){
        std::cout << "Invalid topic name: " << newTopic << std::endl;
        return;
    }

    if(StoreTopicSubscription(perSocketData, newTopic) == false){
        std::cout <<"CharName: "<< perSocketData->name<< " No available topic slots to subscribe to: " << newTopic << std::endl;
        return;
    }
    ws->subscribe(newTopic);
}


void SubscribeGroupTopic(uWS::WebSocket<true, true, PerSocketData>* ws)
{
    PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
    std::string topic;

    if(perSocketData->groupId.empty()){
        std::cout << "No groupId set, so no need to subscribe" << std::endl;
        return;
    }

    if(perSocketData->groupId.rfind("group_", 0) == 0){
        topic = perSocketData->groupId;
    }else{
        topic = groupContainer->GetGroupTopic(perSocketData->groupId);
    }
    // Remove old group topic subscription
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << "Subscribed to standard topic: " << topic << std::endl;
    }

}
void SubscribeStandardTopics(uWS::WebSocket<true, true, PerSocketData>* ws)
{
    PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
    std::string topic;

    topic = "World";
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << "Subscribed to standard topic: " << topic << std::endl;
    }
    topic = "zone_"+perSocketData->zone;
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << "Subscribed to standard topic: " << topic << std::endl;
    }
    topic = "tell_"+ToLower(perSocketData->name);
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << "Subscribed to standard topic: " << topic << std::endl;
    }
    SubscribeGroupTopic(ws);
}

void SendGroupUpdateInfo(uWS::SSLApp *app, uWS::OpCode opCode, std::string groupId)
{
    std::string topic=groupId;
    FDaraChatMsg msg;
    msg.ChatType= "Cmd";
    msg.ChatCmdType= "GroupInfo";
    msg.Sender= "GroupAdmin";
    msg.Recipient= "Group";

    GroupInfo info=groupContainer->GetGroupInfoByGroupId(groupId);
    msg.Msg= info.SerializeToSend();

    std::cout << "Sending GroupInfo: " << topic << ":" << msg.SerializeToPost() << std::endl;    
    app->publish(topic, msg.SerializeToPost(), opCode);

}

void ShowBinaryMessage(const std::vector<uint8_t>& data)
{
    FDaraRawChatMsg msg;
    if(msg.Deserialize(data)){
        std::cout << "Linux Server Message:\n";
        std::cout << "Location: (" << msg.Location.X << ", " << msg.Location.Y << ", " << msg.Location.Z << ")\n";
        std::cout << "Health: " << msg.Health << "\n";
        std::cout << "MaxHealth: " << msg.MaxHealth << "\n";
        std::cout << "Mana: " << msg.Mana << "\n";
        std::cout << "MaxMana: " << msg.MaxMana << "\n";
        std::cout << "Energy: " << msg.Energy << "\n";
        std::cout << "MaxEnergy: " << msg.MaxEnergy << "\n";

    }else{
        std::cout << "Error: Binary format not ok"<<std::endl;
    };
}

FDaraRawChatMsg GetRawChatMsgFromBinaryAlive(const std::vector<uint8_t>& data)
{
    FDaraRawChatMsg msg;
    if(msg.Deserialize(data)){
        return msg;
    }else{
        std::cout << "Error: Binary format not ok"<<std::endl;
        return msg;
    };
}


int main() {

    // this is the keyfile for later on /etc/letsencrypt/live/gameinfo.daraempire.com/privkey.pem
    /* Keep in mind that uWS::SSLApp({options}) is the same as uWS::App() when compiled without SSL support.
     * You may swap to using uWS:App() if you don't need SSL */
    chatLibrary = new uDaraChatLibrary();
    groupContainer= new GroupContainer();

    chatLibrary->SetDebugLevel(10);
    groupContainer->SetDebugLevel(10);

    //groupContainer->RunTests();
    //return 0;


    std::cout<< "**********************Starting uDaraChatServer***********"<<std::endl;  
 


    uWS::SSLApp *app = new uWS::SSLApp({
        /* There are example certificates in uWebSockets.js repo */
        .key_file_name = "misc/key.pem",
        .cert_file_name = "misc/cert.pem",
        .passphrase = "1234"
    });
    
    app->ws<PerSocketData>("/*", {
        /* Settings */
        .compression = uWS::DISABLED,
        .maxPayloadLength = 16 * 1024 * 1024,
        .idleTimeout = 60,
        .maxBackpressure = 16 * 1024 * 1024,
        .closeOnBackpressureLimit = false,
        .resetIdleTimeoutOnSend = true,
        .sendPingsAutomatically = true,
        /* Handlers */
        .upgrade = nullptr,
        .open = [](auto *ws) {
            /* Open event here, you may access ws->getUserData() which points to a PerSocketData struct */

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();

            for (int i = 0; i < 32; i++) {
                TopicSlot topic;
                topic.topic= std::to_string((uintptr_t)ws) + "-" + std::to_string(i);
                topic.used=false;
                perSocketData->topics.push_back(topic);
                ws->subscribe(topic.topic);
            }
            std::string_view remoteAddress = ws->getRemoteAddressAsText();
            std::string remoteAddressIPV4= ConvertMappedIPv6ToIPv4(std::string(remoteAddress));
            std::cout << "Log: Connect from Client IP: " << remoteAddressIPV4 << std::endl;
            
            
        },
        .message = [&app](auto *ws, std::string_view message, uWS::OpCode opCode) {
            static int messageCount = 0;
            static bool scheduleFlip= false;
            messageCount++;
            
            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            if(opCode!=1){
                // we are just resending the info in the group channel so everybody can use it
                std::cout << "*** Message Received: OpCode: " << static_cast<int>(opCode)<< std::endl;
                std::string topic=perSocketData->groupId;
                std::vector<uint8_t> data(message.begin(), message.end());
                //ShowBinaryMessage(data);
                app->publish(topic, message, opCode);
                // now we keep our groupmembership alive ... we are not LD yet
                GroupMember member;
                member.name=perSocketData->name;
                member.zone=perSocketData->zone;
                FDaraRawChatMsg msg= GetRawChatMsgFromBinaryAlive(data);
                float HealthPercentage= msg.Health/msg.MaxHealth;
                member.health= std::to_string(HealthPercentage); 
                std::cout << "Alive: " << member.name << ":" << member.zone << ":" << member.health << std::endl;
                groupContainer->AliveMsg(member);
                return;
            }
            std::cout << "*** Message Received: OpCode: " << static_cast<int>(opCode) << " Msg:" << message << std::endl;
            std::string msg = std::string(message);
            
            
            if (msg.rfind("connect:", 0) == 0) {
                // get remote address
                std::string_view remoteAddress = ws->getRemoteAddressAsText();
                std::string remoteAddressIPV4= ConvertMappedIPv6ToIPv4(std::string(remoteAddress));
                // handle connect
                if(HandleConnect(perSocketData, msg, remoteAddressIPV4)){
                    // setting groupId from group container
                    perSocketData->groupId= groupContainer->GetCurrentGroupId(perSocketData->name).groupId;
                    std::cout << "Log: "<< remoteAddressIPV4 << " Client " + perSocketData->name + " connected with groupid:"<< perSocketData->groupId << std::endl;
                    SubscribeStandardTopics(ws);
                    if(!perSocketData->groupId.empty()){
                        std::cout << "GroupUpdate to all" << std::endl;
                        SendGroupUpdateInfo(app, opCode, perSocketData->groupId);
                    }
                }
                return;
            }
            if(perSocketData->connected==false){
                // get remote address
                std::string_view remoteAddress = ws->getRemoteAddressAsText();
                std::string remoteAddressIPV4= ConvertMappedIPv6ToIPv4(std::string(remoteAddress));
                std::cout <<"ERROR: "<< remoteAddressIPV4<< " Client " + perSocketData->name +" not connected, ignoring message"<< std::endl;
                return;
            }

            if (msg.rfind("schedule:", 0) == 0) {
                if(scheduleFlip==false){
                    std::vector<std::string> groupIds= groupContainer->GetUniqueGroupIds();
                    for (auto& groupId : groupIds) {
                        SendGroupUpdateInfo(app, opCode, groupId);
        
                    }
                    groupContainer->DumpGroups();
                    scheduleFlip= true;
                }
                else{
                    groupContainer->RemoveLDGroupMembers();
                    groupContainer->DumpGroups();
                    scheduleFlip= false;
                }
            }
            if (msg.rfind("subscribe:", 0) == 0) {
                HandleSubscribeMessage(ws, msg.substr(10)); 
                DumpTopicSubscription(perSocketData);
                return;
            }

            if (msg.rfind("unsubscribe:", 0) == 0) {
                RemoveTopicSubscription(ws, perSocketData, msg.substr(12));
                DumpTopicSubscription(perSocketData);
                return;
            }

            FDaraChatMsg ChatMsg= chatLibrary->ParseSentMessage(msg);
            std::cout << "***** Parsed ChatMsg: " << ChatMsg.ChatType<<"|"<< ChatMsg.ChatCmdType <<"|"<<ChatMsg.Sender<<"|"<<ChatMsg.Recipient<<"|"<<ChatMsg.Msg << std::endl;

            if (ChatMsg.ChatType=="Channel") {
                app->publish(ChatMsg.Recipient, ChatMsg.SerializeToPost(), opCode);
                std::cout << "Sending World: " << "World : " << ChatMsg.SerializeToPost() << std::endl;
                return;
            }
            if (ChatMsg.ChatType=="Zone") {
                std::string topic=ChatMsg.getTopicPrefix()+perSocketData->zone; 
                app->publish(topic,ChatMsg.SerializeToPost(), opCode);
                std::cout << "Sending Zone: " << topic << " : " << ChatMsg.SerializeToPost() << std::endl;
                return;
            }
            if (ChatMsg.ChatType=="Group") {
                if(perSocketData->groupId.empty()){
                    std::cout << "Trying to send Group message with no group yet: " << ChatMsg.Sender << std::endl;
                    return;
                }
                std::string topic=perSocketData->groupId;
                app->publish(topic, ChatMsg.SerializeToPost(), opCode);
                std::cout << "Sending Group: " << topic << " : " << ChatMsg.SerializeToPost() << std::endl;
                return;
            }
            if (ChatMsg.ChatType=="Tell") {
                if(ChatMsg.Msg.rfind("DEBUG", 0) == 0){
                    std::cout << "DEBUG: Messages received: " << std::to_string(messageCount) << std::endl;
                    groupContainer->DumpGroups();
                    groupContainer->DumpInvites();
                    DumpTopicSubscription(perSocketData);
                    return;
                }
                std::string topic=ChatMsg.getTopicPrefix()+ToLower(ChatMsg.Recipient);
                app->publish(topic, ChatMsg.SerializeToPost(), opCode);
                std::cout << "Sending Tell: " << topic << " : " << ChatMsg.SerializeToPost() << std::endl;
                return;
            }


            if(ChatMsg.ChatCmdType=="GroupInvite"){
                std::string Inviter=ChatMsg.Sender;
                std::string Invited=ChatMsg.Recipient;
                std::cout << "GroupInvite: " << ChatMsg.Sender << " inviting " << ChatMsg.Recipient << " to group " << ChatMsg.Msg << std::endl;
                groupContainer->AddInvite(Inviter, Invited);

                // Check if the sender is already in a group otherwise start one
                if(perSocketData->groupId.empty()){
                    std::cout << "GroupInvite with no group yet: " << ChatMsg.Sender << std::endl;
                    GroupInfo info=groupContainer->GroupStartGroup(ChatMsg.Sender);

                    perSocketData->groupId=info.groupId;
                    SubscribeGroupTopic(ws);
                    DumpTopicSubscription(perSocketData);
                } 
        
                // we must forward  this message to the tell_ topic channel
                ChatMsg.ChatType="Tell";
                std::string telltopic=ChatMsg.getTopicPrefix()+ToLower(Invited);
                ChatMsg.ChatType="Cmd";
                ChatMsg.ChatCmdType="GroupInvite";
                app->publish(telltopic, ChatMsg.SerializeToPost(), opCode);
                std::cout << "Sending Cmd: " << telltopic << " : " << ChatMsg.SerializeToPost() << std::endl;
                return;
            }
            if(ChatMsg.ChatCmdType=="GroupJoin"){
                std::string Inviter=ChatMsg.Recipient;
                std::string Invited=ChatMsg.Sender;
                std::cout << "GroupJoin: " << Invited << " wants to join group of " << Inviter <<std::endl;
                // remove outdated invites
                groupContainer->RemoveOldInvites();
                // did we get an invite?
                if(groupContainer->CheckHasInvited(Inviter, Invited) == false){
                    std::cout << "GroupJoin: " << Inviter << " has not invited " << Invited << " to a group" <<std::endl;
                    return;
                }
                // Check if the sender is already in a group otherwise start one
                GroupInfo info=groupContainer->GetGroupInfo(Invited);
                if(info.memberCount==1){
                    //can leave group as I am solo in there and want to join other group
                    groupContainer->GroupDisband(Invited);
                    perSocketData->groupId=""; 
                }
                std::cout << "GroupJoin: " << Invited << " has Membercount " << std::to_string(info.memberCount) <<std::endl;

                if(perSocketData->groupId.empty() ){
                    std::cout << "GroupJoin: Joining group. Got GroupInvite with no group yet: " << ChatMsg.Sender << std::endl;
                    GroupInfo info=groupContainer->GroupJoin(Invited, Inviter);
                    
                    // New Group
                    RemoveGroupSubscription(ws);
                    perSocketData->groupId=info.groupId;
                    SubscribeGroupTopic(ws);
                    DumpTopicSubscription(perSocketData);

                    std::cout << ChatMsg.Sender << " new Group is now: " << info.groupId << std::endl;
                    groupContainer->RemoveInvite(Inviter, Invited);

                    FDaraChatMsg msg;
                    msg.ChatType= "Group";
                    msg.Sender= perSocketData->name;
                    std::string newGroupTopic= perSocketData->groupId;
                    msg.ChatType= "Cmd";
                    msg.ChatCmdType= "GroupJoinInfo";
                    //app->publish(newGroupTopic, chatLibrary->GetGroupJoinInfoMessage(msg), opCode);    
                    SendGroupUpdateInfo(app, opCode, newGroupTopic);

                }else{
                    std::cout << "GroupJoin: " << ChatMsg.Sender << " cannot join as in another group:  " <<  perSocketData->groupId << std::endl;
                }
                groupContainer->DumpGroups();// shall we let this message pass as it shall be published to the recipient?
                return;
            }
            if(ChatMsg.ChatCmdType=="GroupDisband"){
                if(!perSocketData->groupId.empty()){
                    // disband from old group if not empty
                    std::cout << "GroupDisband: " << ChatMsg.Sender << " former groupId: " << perSocketData->groupId<< std::endl;
                    std::string oldGroupTopic= groupContainer->GetGroupTopic(perSocketData->groupId);

                    GroupInfo info=groupContainer->GroupDisband(ChatMsg.Sender);
                    RemoveGroupSubscription(ws);
                    perSocketData->groupId="";
                    DumpTopicSubscription(perSocketData);

                    FDaraChatMsg msg;
                    msg.ChatType= "Cmd";
                    msg.ChatCmdType= "GroupDisbandInfo";
                    msg.Sender= perSocketData->name;
                    app->publish(oldGroupTopic, chatLibrary->GetGroupDisbandInfoMessage(msg), opCode);    
                    SendGroupUpdateInfo(app, opCode, oldGroupTopic);
                }else{
                    std::cout << "Received GroupDisband with no group ! " << ChatMsg.Sender << std::endl;
                }
                return;
            }
            if(ChatMsg.ChatCmdType=="Alive"){
                GroupMember member;
                member.name=ChatMsg.Sender;
                member.zone=perSocketData->zone;
                member.health=ChatMsg.Msg;
                std::cout << "Alive: " << member.name << ":" << member.zone << ":" << member.health << std::endl;
                groupContainer->AliveMsg(member);

                FDaraChatMsg msg;
                msg.ChatType= "Cmd";
                msg.ChatCmdType= "AliveInfo";
                msg.Sender=ChatMsg.Sender;
                msg.Recipient= "Group";
                msg.Msg= member.zone + ";"+ ChatMsg.Msg;
                std::string topic=perSocketData->groupId;
                app->publish(topic, msg.SerializeToPost(), opCode);
                std::cout << "Sending AliveInfo: " << topic << ":" << msg.SerializeToPost() << std::endl;
                return;
            }

            std::cout << "ERROR: Why are we here? Should never come to here" << msg << std::endl;
            // Optional: handle publishing
            if (msg.rfind("publish:", 0) == 0) {
                auto sep = msg.find(':', 8);
                if (sep != std::string::npos) {
                    std::string topic = msg.substr(8, sep - 8);
                    // old version: std::string payload = msg.substr(sep + 1);
                    std::string payload = msg.substr(8);
                    //payload=topic+":"+perSocketData->name + ":" + msg;
                    app->publish(topic, payload, opCode);
                    if(DEBUGLEVEL>9){
                        std::cout << "Message Publish   : " << topic << " : " << payload << std::endl;
                    }
                }
                return;
            }

            //ChatMessage chatMsg=chatLibrary->ParseChatMessage(msg);
            //std::cout << "GroupInvite: " << chatMsg.sender << " Inviting " << chatMsg.receiver << " to group " << chatMsg.message << std::endl; 
        },
        .drain = [](auto */*ws*/) {
            /* Check ws->getBufferedAmount() here */
            //std::cout << "drain" << std::endl;
        },
        .ping = [](auto */*ws*/, std::string_view ) {
            /* Not implemented yet */
        },
        .pong = [](auto */*ws*/, std::string_view ) {
            /* Not implemented yet */
        },
        .close = [](auto */*ws*/, int /*code*/, std::string_view /*message*/) {
            /* You may access ws->getUserData() here */
        }
    }).listen(9020, [](auto *listen_s) {
        if (listen_s) {
            std::cout << "Listening on port " << 9020 << std::endl;
            //listen_socket = listen_s;
        }
    });
    
    app->run();

    delete app;

    uWS::Loop::get()->free();
}
