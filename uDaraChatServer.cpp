#include "AdminQueries.h"
#include "App.h"
#include "ChatLibrary.h"
#include "GroupContainer.h"


#include "JwtAuth.h"
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <openssl/err.h>


#ifdef LIBUS_NO_SSL
#error "JWT bearer authentication requires TLS support"
#endif
#include <sstream>
#include <iomanip>

#include <arpa/inet.h>      // For inet_ntop
#include <netinet/in.h>     // For sockaddr_in and sockaddr_in6
#include <cstring>          // For memset if needed
#include <unordered_map>


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

    bool isAdmin = false;
    jwt::date adminExpiresAt{};
    bool connected = false;
};
using DaraSocket = uWS::WebSocket<true, true, PerSocketData>;
std::unordered_map<std::string, DaraSocket*> activeSessions;

const auto serverStarted = std::chrono::steady_clock::now();

dara::AdminSnapshot CollectAdminSnapshot() {
    dara::AdminSnapshot state;
    state.uptimeSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - serverStarted).count();
    state.connectedClients = activeSessions.size();
    for (const auto& id : groupContainer->GetUniqueGroupIds())
        state.groups.push_back(groupContainer->GetGroupInfoByGroupId(id));
    for (const auto& session : activeSessions) {
        const auto* data = session.second->getUserData();
        ++state.zones[data->zone];
        std::unordered_set<std::string> channels;
        for (const auto& slot : data->topics)
            if (slot.used && slot.topic.rfind("tell_", 0) != 0 &&
                    slot.topic.rfind("group_", 0) != 0 && slot.topic.rfind("zone_", 0) != 0)
                channels.insert(slot.topic);
        for (const auto& channel : channels) ++state.channels[channel];
    }
    return state;
}


std::string TimeStamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&time, &tm);  // Linux, thread-safe

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H:%M:%S ");

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

bool HandleConnect(PerSocketData *data, const std::string& message,
                   const std::string& secret, std::string& failureReason) {
    auto login = dara::VerifyLogin(message, secret, std::chrono::system_clock::now(), &failureReason);
    if (!login) return false;
    data->name = login->characterName;
    data->zone = login->zone;
    data->isAdmin = login->isAdmin;
    data->adminExpiresAt = login->expiresAt;
    data->connected = true;
    return true;
}

bool DumpTopicSubscription(PerSocketData *data)
{
    std::cout << TimeStamp() << "Dumping topic subscriptions for: " << data->name << std::endl;
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];
        if (slot.used) {
            std::cout << TimeStamp()  << "Topic slot " << i << ":" << slot.topic << std::endl;
        }
    }
    return true;
}



bool RemoveGroupSubscription(uWS::WebSocket<true, true, PerSocketData>* ws)
{
    PerSocketData *data = (PerSocketData *) ws->getUserData();
    std::string oldGroupTopic;
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];

        if (slot.used && slot.topic.rfind("group_", 0) == 0) {
            oldGroupTopic = slot.topic;
            data->topics[i].topic= "none";
            data->topics[i].used= false;
            std::cout << TimeStamp() << data->name << " Removed group topic in slot " << i << ":" << oldGroupTopic << std::endl;
            ws->unsubscribe(oldGroupTopic);
            return true;
        }
    }
    std::cout << TimeStamp() << "Old group topic slot not found so no need to unsusbcribe" << std::endl;
    return false;
}

bool RemoveTopicSubscription(uWS::WebSocket<true, true, PerSocketData>* ws,PerSocketData *data, const std::string& oldTopic)
{
    for (size_t i = 0; i < data->topics.size(); ++i) {
        TopicSlot& slot = data->topics[i];

        if (slot.used && slot.topic == oldTopic) {
            data->topics[i].topic= "none";
            data->topics[i].used= false;
            std::cout << TimeStamp() << data->name << " Removed topic in slot " << i << ":" << oldTopic << std::endl;
            ws->unsubscribe(oldTopic);
            return true;
        }
    }
    std::cout << TimeStamp() << "Old topic slot not found so no need to unsusbcribe: " << oldTopic << std::endl;
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
            std::cout << TimeStamp() << data->name << " Subscribed to new topic in slot " << i << ":" << newTopic << std::endl;
            return true;
        }
        if(slot.topic == newTopic && slot.used==true) {
            std::cout << TimeStamp() << data->name << " Already Subscribed to topic in slot " << i << ":" << newTopic << std::endl;
            return true;
        }
    }
    std::cout << TimeStamp() << "No available topic slots to subscribe to: " << newTopic << std::endl;
    return false;
}

bool CheckValidTopicName(const std::string& topicName) {
    // tell channels are only for direct tells. Therefore they are forbidden
    if (topicName.rfind("tell_", 0) == 0 || topicName == "World") {
        std::cout << TimeStamp() << "Invalid topic reserved for a private tell channel: " << topicName << std::endl;
        return false;
    }
    // zone channels are only for the zone server. Therefore they are forbidden
    if (topicName.rfind("group_", 0) == 0 || topicName.rfind("zone_", 0) == 0) {
        std::cout << TimeStamp() << "Invalid topic reserved for a zone channel: " << topicName << std::endl;
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
        std::cout << TimeStamp() << "Invalid topic name: " << newTopic << std::endl;
        return;
    }

    if(StoreTopicSubscription(perSocketData, newTopic) == false){
        std::cout << TimeStamp() <<"CharName: "<< perSocketData->name<< " No available topic slots to subscribe to: " << newTopic << std::endl;
        return;
    }
    ws->subscribe(newTopic);
}


void SubscribeGroupTopic(uWS::WebSocket<true, true, PerSocketData>* ws)
{
    PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
    std::string topic;

    if(perSocketData->groupId.empty()){
        std::cout << TimeStamp() << "No groupId set, so no need to subscribe" << std::endl;
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
        std::cout << TimeStamp() << "Subscribed to standard topic: " << topic << std::endl;
    }

}
void SubscribeStandardTopics(uWS::WebSocket<true, true, PerSocketData>* ws)
{
    PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
    std::string topic;

    topic = "World";
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << TimeStamp() << "Subscribed to standard topic: " << topic << std::endl;
    }
    topic = "zone_"+perSocketData->zone;
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << TimeStamp() << "Subscribed to standard topic: " << topic << std::endl;
    }
    topic = "tell_"+ToLower(perSocketData->name);
    if(StoreTopicSubscription(perSocketData, topic) == true){
        ws->subscribe(topic);
        std::cout << TimeStamp() << "Subscribed to standard topic: " << topic << std::endl;
    }
    SubscribeGroupTopic(ws);
}

void SendGroupUpdateInfo(uWS::SSLApp *app, uWS::OpCode opCode, std::string groupId)
{
    std::string topic=groupContainer->GetGroupTopic(groupId);
    FDaraChatMsg msg;

    msg.Type = EChatCmdType::GroupInfo;
    msg.Sender= "GroupAdmin";
    msg.Recipient = "";

    GroupInfo info=groupContainer->GetGroupInfoByGroupId(groupId);
    msg.Message= info.SerializeToSend();

    std::cout << TimeStamp() << "Sending GroupInfo: " << topic << ":" << msg.Serialize() << std::endl;
    app->publish(topic, msg.Serialize(), opCode);

}

void ShowBinaryMessage(const std::vector<uint8_t>& data)
{
    FDaraRawChatMsg msg;
    if(msg.Deserialize(data)){
        std::cout << TimeStamp() << "Linux Server Message:\n";
        std::cout << "Location: (" << msg.Location.X << ", " << msg.Location.Y << ", " << msg.Location.Z << ")\n";
        std::cout << "Health: " << msg.Health << "\n";
        std::cout << "MaxHealth: " << msg.MaxHealth << "\n";
        std::cout << "Mana: " << msg.Mana << "\n";
        std::cout << "MaxMana: " << msg.MaxMana << "\n";
        std::cout << "Energy: " << msg.Energy << "\n";
        std::cout << "MaxEnergy: " << msg.MaxEnergy << "\n";

    }else{
        std::cout << TimeStamp() << "Error: Binary format not ok"<<std::endl;
    };
}

FDaraRawChatMsg GetRawChatMsgFromBinaryAlive(const std::vector<uint8_t>& data)
{
    FDaraRawChatMsg msg;
    if(msg.Deserialize(data)){
        return msg;
    }else{
        std::cout << TimeStamp() << "Error: Binary format not ok"<<std::endl;
        return msg;
    };
}


int main() {
    const char* configuredSecret = std::getenv("DARA_CHAT_JWT_SECRET");
    if (!configuredSecret || !*configuredSecret) {
        std::cerr << "DARA_CHAT_JWT_SECRET must be set and nonempty" << std::endl;
        return 1;
    }
    const std::string jwtSecret(configuredSecret); // Raw UTF-8 bytes, no decoding.


    constexpr const char* tlsKey = "/etc/letsencrypt/live/gameinfo.daraempire.com/privkey.pem";
    constexpr const char* tlsChain = "/etc/letsencrypt/live/gameinfo.daraempire.com/fullchain.pem";
    bool filesReadable = true;
    for (const char* path : {tlsKey, tlsChain}) {
        FILE* file = std::fopen(path, "rb");
        if (!file) {
            const int error = errno;
            std::cerr << "TLS startup failed: cannot open " << path << ": "
                      << std::strerror(error) << " (errno " << error << "). "
                      << "The service user needs read access to the file and traversal access "
                      << "to all parent directories, including the Let's Encrypt archive symlink target."
                      << std::endl;
            filesReadable = false;
        } else {
            std::fclose(file);
        }
    }
    if (!filesReadable) return 1;
    chatLibrary = new uDaraChatLibrary();
    groupContainer= new GroupContainer();

    chatLibrary->SetDebugLevel(10);
    groupContainer->SetDebugLevel(10);

    //groupContainer->RunTests();
    //return 0;


    std::cout << TimeStamp() << "**********************Starting uDaraChatServer***********"<<std::endl;



    ERR_clear_error();
    uWS::SSLApp *app = new uWS::SSLApp({
        .key_file_name = tlsKey,
        .cert_file_name = tlsChain
    });
    if (app->constructorFailed()) {
        std::cerr << "TLS startup failed: could not load private key " << tlsKey
                  << " and certificate chain " << tlsChain
                  << ". Check PEM format, matching key/certificate and file permissions." << std::endl;
        while (const unsigned long error = ERR_get_error()) {
            char detail[256];
            ERR_error_string_n(error, detail, sizeof(detail));
            std::cerr << "OpenSSL: " << detail << std::endl;
        }
        delete app;
        delete groupContainer;
        delete chatLibrary;
        return 1;
    }
    bool listening = false;

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
            std::cout << TimeStamp() << "[WS OPEN] Awaiting JWT login; ip=" << remoteAddressIPV4 << std::endl;


        },
        .message = [&app, &jwtSecret](auto *ws, std::string_view message, uWS::OpCode opCode) {
            static int messageCount = 0;
            static bool scheduleFlip= false;
            messageCount++;

            PerSocketData *perSocketData = (PerSocketData *) ws->getUserData();
            if(opCode!=1){
                // we are just resending the info in the group channel so everybody can use it
                std::cout << TimeStamp() << "*** Message Received: OpCode: " << static_cast<int>(opCode)<< std::endl;
                std::string topic=perSocketData->groupId;
                std::vector<uint8_t> data(message.begin(), message.end());
                //ShowBinaryMessage(data);
                if (!perSocketData->connected || perSocketData->groupId.empty()) return;
                FDaraRawChatMsg validated;
                if (!validated.Deserialize(data) || validated.MaxHealth <= 0) return;
                app->publish(topic, message, opCode);
                // now we keep our groupmembership alive ... we are not LD yet
                GroupMember member;
                member.name=perSocketData->name;
                member.zone=perSocketData->zone;
                FDaraRawChatMsg msg= validated;
                float HealthPercentage= msg.Health/msg.MaxHealth;
                member.health= std::to_string(HealthPercentage);
                std::cout << TimeStamp() << "Alive: " << member.name << ":" << member.zone << ":" << member.health << std::endl;
                groupContainer->AliveMsg(member);
                return;
            }
            // Do not log raw text frames: login frames contain bearer credentials.
            std::string msg = std::string(message);


            if (msg.rfind("connect:", 0) == 0) {
                if (perSocketData->connected) {
                    std::cout << TimeStamp() << "[AUTH REJECTED] Socket is already authenticated; open a new WebSocket to reauthenticate" << std::endl;
                    ws->end(1008, "Reconnect to reauthenticate");
                    return;
                }
                // get remote address
                std::string_view remoteAddress = ws->getRemoteAddressAsText();
                std::string remoteAddressIPV4= ConvertMappedIPv6ToIPv4(std::string(remoteAddress));
                // handle connect
                std::string failureReason;
                if(HandleConnect(perSocketData, msg, jwtSecret, failureReason)){
                    // setting groupId from group container
                    // One authenticated connection per character. Retire the old socket on reconnect.
                    auto old = activeSessions.find(perSocketData->name);
                    if (old != activeSessions.end() && old->second != ws) old->second->end(1000, "Reconnected");
                    activeSessions[perSocketData->name] = ws;
                    perSocketData->groupId= groupContainer->GetCurrentGroupId(perSocketData->name).groupId;
                    groupContainer->SetMemberOnline(perSocketData->name, true);
                    std::cout << TimeStamp() << "Log: "<< remoteAddressIPV4 << " Client " + perSocketData->name + " connected with groupid:"<< perSocketData->groupId << std::endl;
                    SubscribeStandardTopics(ws);
                    if(!perSocketData->groupId.empty()){
                        std::cout << TimeStamp() << "GroupUpdate to all" << std::endl;
                        SendGroupUpdateInfo(app, opCode, perSocketData->groupId);
                    }
                } else {
                    std::cout << TimeStamp() << "[AUTH REJECTED] ip=" << remoteAddressIPV4
                              << " reason=" << failureReason << std::endl;
                    ws->end(1008, "Authentication failed");
                }
                return;
            }
            if(perSocketData->connected==false){
                // get remote address
                std::string_view remoteAddress = ws->getRemoteAddressAsText();
                std::string remoteAddressIPV4= ConvertMappedIPv6ToIPv4(std::string(remoteAddress));
                std::cout << TimeStamp() <<"ERROR: "<< remoteAddressIPV4<< " [AUTH REQUIRED] Message ignored: WebSocket is open but login has not succeeded; send connect:name=<characterName>;zone=<zone>;group=;pass=<JWT> first"<< std::endl;
                return;
            }

            if (msg.rfind("schedule:", 0) == 0) {
                return; // Reserved internal maintenance command; not available to clients.

                if(scheduleFlip==false){
                    std::vector<std::string> groupIds= groupContainer->GetUniqueGroupIds();
                    for (auto& groupId : groupIds) {
                        SendGroupUpdateInfo(app, opCode, groupId);

                    }
                    groupContainer->DumpGroups();
                    scheduleFlip= true;
                }
                else{
                    // Keep offline members in their groups. WebSocket close handles presence.
                    groupContainer->DumpGroups();
                    scheduleFlip= false;
                }
            }
            if (msg.rfind("subscribe:", 0) == 0) {
                HandleSubscribeMessage(ws, msg);
                DumpTopicSubscription(perSocketData);
                return;
            }

            if (msg.rfind("unsubscribe:", 0) == 0) {
                RemoveTopicSubscription(ws, perSocketData, msg.substr(12));
                DumpTopicSubscription(perSocketData);
                return;
            }

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

            FDaraChatMsg ChatMsg;
            std::string parseError;
            if (!chatLibrary->TryParseMessage(msg, ChatMsg, parseError)) return;
            // Never trust the sender field supplied by the client.
            ChatMsg.Sender = perSocketData->name;
            if (dara::IsAdminQuery(ChatMsg.Type) || ChatMsg.Type == EChatCmdType::AdminInfo ||
                    ChatMsg.Type == EChatCmdType::AdminError) {
                const auto reply = dara::AnswerAdminQuery(ChatMsg, perSocketData->isAdmin &&
                    std::chrono::system_clock::now() < perSocketData->adminExpiresAt, CollectAdminSnapshot);
                ws->send(reply.Serialize(), uWS::OpCode::TEXT);
                return;
            }

            std::cout << TimeStamp() << "***** Parsed ChatMsg: " << ChatCmdTypeToString(ChatMsg.Type) <<"|"<<ChatMsg.Sender<<"|"<<ChatMsg.Recipient<<"|"<<ChatMsg.Message << std::endl;

            if (ChatMsg.Type == EChatCmdType::Channel) {
                app->publish(ChatMsg.Recipient, ChatMsg.Serialize(), opCode);
                std::cout << TimeStamp() << "Sending World: " << "World : " << ChatMsg.Serialize() << std::endl;
                return;
            }
            if (ChatMsg.Type == EChatCmdType::Zone) {
                std::string topic="zone_"+perSocketData->zone;
                app->publish(topic,ChatMsg.Serialize(), opCode);
                std::cout << TimeStamp() << "Sending Zone: " << topic << " : " << ChatMsg.Serialize() << std::endl;
                return;
            }
            if (ChatMsg.Type == EChatCmdType::Group) {
                if(perSocketData->groupId.empty()){
                    std::cout << TimeStamp() << "Trying to send Group message with no group yet: " << ChatMsg.Sender << std::endl;
                    return;
                }
                std::string topic=perSocketData->groupId;
                app->publish(topic, ChatMsg.Serialize(), opCode);
                std::cout << TimeStamp() << "Sending Group: " << topic << " : " << ChatMsg.Serialize() << std::endl;
                return;
            }
            if (ChatMsg.Type == EChatCmdType::Tell) {
                if(ChatMsg.Message.rfind("DEBUG", 0) == 0){
                    if (!perSocketData->isAdmin || std::chrono::system_clock::now() >= perSocketData->adminExpiresAt) {
                        ws->send(dara::AdminError(perSocketData->name, "Forbidden",
                            "Administrator access required").Serialize(), uWS::OpCode::TEXT);
                        return;
                    }
                    std::cout << TimeStamp() << "DEBUG: Messages received: " << std::to_string(messageCount) << std::endl;
                    groupContainer->DumpGroups();
                    groupContainer->DumpInvites();
                    DumpTopicSubscription(perSocketData);
                    return;
                }
                std::string topic="tell_"+ToLower(ChatMsg.Recipient);
                app->publish(topic, ChatMsg.Serialize(), opCode);
                std::cout << "Sending Tell: " << topic << " : " << ChatMsg.Serialize() << std::endl;
                return;
            }


            if(ChatMsg.Type == EChatCmdType::GroupInvite){
                std::string Inviter=ChatMsg.Sender;
                std::string Invited=ChatMsg.Recipient;
                std::cout << TimeStamp() << "GroupInvite: " << ChatMsg.Sender << " inviting " << ChatMsg.Recipient << " to group " << ChatMsg.Message << std::endl;
                // Check if the sender is already in a group otherwise start one
                if(perSocketData->groupId.empty()){
                    std::cout << "GroupInvite with no group yet: " << ChatMsg.Sender << std::endl;
                    GroupInfo info=groupContainer->GroupStartGroup(ChatMsg.Sender);

                    if (info.errorCode != 0) return;
                    perSocketData->groupId=info.groupId;
                    SubscribeGroupTopic(ws);
                    DumpTopicSubscription(perSocketData);
                }

                GroupInfo inviteCheck = groupContainer->GroupInvite(Invited, Inviter);
                if (inviteCheck.errorCode != 1) return;
                groupContainer->RemoveOldInvites();
                groupContainer->AddInvite(Inviter, Invited);
                // we must forward  this message to the tell_ topic channel
                std::string telltopic="tell_"+ToLower(Invited);

                ChatMsg.Type = EChatCmdType::GroupInvite;
                app->publish(telltopic, ChatMsg.Serialize(), opCode);
                std::cout << TimeStamp() << "Sending Cmd: " << telltopic << " : " << ChatMsg.Serialize() << std::endl;
                return;
            }
            if(ChatMsg.Type == EChatCmdType::GroupJoin){
                std::string Inviter=ChatMsg.Recipient;
                std::string Invited=ChatMsg.Sender;
                std::cout << TimeStamp() << "GroupJoin: " << Invited << " wants to join group of " << Inviter <<std::endl;
                // remove outdated invites
                groupContainer->RemoveOldInvites();
                // did we get an invite?
                if(groupContainer->CheckHasInvited(Inviter, Invited) == false){
                    std::cout << TimeStamp() << "GroupJoin: " << Inviter << " has not invited " << Invited << " to a group" <<std::endl;
                    return;
                }
                // Validate target group before leaving a solo group.
                GroupInfo targetGroup = groupContainer->GetGroupInfo(Inviter);
                if (targetGroup.groupId.empty() || targetGroup.memberCount >= groupContainer->MyMaxGroupMembers) return;
                // Check if the sender is already in a group otherwise start one
                GroupInfo info=groupContainer->GetGroupInfo(Invited);
                if (!info.groupId.empty() && info.groupId == targetGroup.groupId) return;
                if(info.memberCount==1){
                    //can leave group as I am solo in there and want to join other group
                    RemoveGroupSubscription(ws);
                    groupContainer->GroupDisband(Invited);
                    perSocketData->groupId="";
                }
                std::cout << TimeStamp() << "GroupJoin: " << Invited << " has Membercount " << std::to_string(info.memberCount) <<std::endl;

                if(perSocketData->groupId.empty() ){
                    std::cout << TimeStamp() << "GroupJoin: Joining group. Got GroupInvite with no group yet: " << ChatMsg.Sender << std::endl;
                    GroupInfo info=groupContainer->GroupJoin(Invited, Inviter);

                    if (info.errorCode != 0 || info.groupId.empty()) return;
                    // New Group
                    if (info.errorCode != 0) return;
                    perSocketData->groupId=info.groupId;
                    SubscribeGroupTopic(ws);
                    DumpTopicSubscription(perSocketData);

                    std::cout << TimeStamp() << ChatMsg.Sender << " new Group is now: " << info.groupId << std::endl;
                    groupContainer->RemoveInvite(Inviter, Invited);

                    FDaraChatMsg msg;
                    msg.Type = EChatCmdType::Group;
                    msg.Sender= perSocketData->name;
                    std::string newGroupTopic= perSocketData->groupId;

                    msg.Type = EChatCmdType::GroupJoinInfo;
                    //app->publish(newGroupTopic, chatLibrary->GetGroupJoinInfoMessage(msg), opCode);
                    SendGroupUpdateInfo(app, opCode, newGroupTopic);

                }else{
                    std::cout << "GroupJoin: " << ChatMsg.Sender << " cannot join as in another group:  " <<  perSocketData->groupId << std::endl;
                }
                groupContainer->DumpGroups();// shall we let this message pass as it shall be published to the recipient?
                return;
            }
            if(ChatMsg.Type == EChatCmdType::GroupDisband){
                if(!perSocketData->groupId.empty()){
                    // disband from old group if not empty
                    std::cout << TimeStamp() << "GroupDisband: " << ChatMsg.Sender << " former groupId: " << perSocketData->groupId<< std::endl;
                    std::string oldGroupTopic= groupContainer->GetGroupTopic(perSocketData->groupId);

                    GroupInfo info=groupContainer->GroupDisband(ChatMsg.Sender);
                    RemoveGroupSubscription(ws);
                    perSocketData->groupId="";
                    DumpTopicSubscription(perSocketData);

                    FDaraChatMsg msg;

                    msg.Type = EChatCmdType::GroupDisbandInfo;
                    msg.Sender= perSocketData->name;
                    app->publish(oldGroupTopic, chatLibrary->GetGroupDisbandInfoMessage(msg), opCode);
                    SendGroupUpdateInfo(app, opCode, oldGroupTopic);
                }else{
                    std::cout << TimeStamp() << "Received GroupDisband with no group ! " << ChatMsg.Sender << std::endl;
                }
                return;
            }
            if(ChatMsg.Type == EChatCmdType::Alive){
                GroupMember member;
                member.name=ChatMsg.Sender;
                member.zone=perSocketData->zone;
                member.health=ChatMsg.Message;
                std::cout << TimeStamp() << "Alive: " << member.name << ":" << member.zone << ":" << member.health << std::endl;
                groupContainer->AliveMsg(member);

                FDaraChatMsg msg;

                msg.Type = EChatCmdType::AliveInfo;
                msg.Sender=ChatMsg.Sender;
                msg.Recipient = "";
                msg.Message= member.zone + ";"+ ChatMsg.Message;
                std::string topic=perSocketData->groupId;
                app->publish(topic, msg.Serialize(), opCode);
                std::cout << TimeStamp() << "Sending AliveInfo: " << topic << ":" << msg.Serialize() << std::endl;
                return;
            }

            std::cout << TimeStamp() << "ERROR: Why are we here? Should never come to here" << msg << std::endl;
            //ChatMessage chatMsg=chatLibrary->ParseChatMessage(msg);
            //std::cout << TimeStamp() << "GroupInvite: " << chatMsg.sender << " Inviting " << chatMsg.receiver << " to group " << chatMsg.message << std::endl;
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
        .close = [&app](auto *ws, int /*code*/, std::string_view /*message*/) {
            auto* data = ws->getUserData();
            if (!data->connected) return;
            auto it = activeSessions.find(data->name);
            if (it == activeSessions.end() || it->second != ws) return;
            activeSessions.erase(it);
            if (groupContainer->SetMemberOnline(data->name, false) && !data->groupId.empty())
                SendGroupUpdateInfo(app, uWS::OpCode::TEXT, data->groupId);
        }
    }).listen(9020, [&listening](auto *listen_s) {
        if (listen_s) {
            listening = true;
            std::cout << TimeStamp() << "Listening on wss://gameinfo.daraempire.com:9020/" << std::endl;
            //listen_socket = listen_s;
        }
    });

    if (!listening) {
        std::cerr << "TLS startup failed: cannot listen on port 9020; check whether another process already owns the port." << std::endl;
        delete app;
        delete groupContainer;
        delete chatLibrary;
        return 1;
    }
    app->run();

    delete app;

    uWS::Loop::get()->free();
}
