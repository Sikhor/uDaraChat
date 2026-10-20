#pragma once
#include "ChatLibrary.h"
#include "GroupContainer.h"
#include <jwt-cpp/jwt.h>
#include <algorithm>
#include <map>
#include <functional>

namespace dara {
struct AdminSnapshot {
    double uptimeSeconds = 0;
    size_t connectedClients = 0;
    std::vector<GroupInfo> groups;
    std::map<std::string, size_t> zones;
    std::map<std::string, size_t> channels;
};
inline bool IsAdminQuery(EChatCmdType type) {
    return type == EChatCmdType::AdminStatus || type == EChatCmdType::AdminGroups ||
           type == EChatCmdType::AdminZones || type == EChatCmdType::AdminChannels;
}
inline FDaraChatMsg AdminError(const std::string& name, const std::string& code,
                              const std::string& message) {
    FDaraChatMsg reply;
    reply.Type = EChatCmdType::AdminError;
    reply.Sender = "Server";
    reply.Recipient = name;
    reply.Message = picojson::value(picojson::object{
        {"code", picojson::value(code)}, {"message", picojson::value(message)}}).serialize();
    return reply;
}
// Authorization precedes collecting any server state.
inline FDaraChatMsg AnswerAdminQuery(const FDaraChatMsg& request, bool isAdmin,
                                    const std::function<AdminSnapshot()>& collect) {
    if (!isAdmin) return AdminError(request.Sender, "Forbidden", "Administrator access required");
    if (!IsAdminQuery(request.Type) || !request.Recipient.empty() || !request.Message.empty())
        return AdminError(request.Sender, "InvalidQuery", "Expected an admin query with empty recipient and message");
    const auto state = collect();
    picojson::object body;
    std::string query;
    if (request.Type == EChatCmdType::AdminStatus) {
        query = "status";
        size_t onlineGroups = 0;
        for (const auto& group : state.groups)
            if (std::any_of(group.groupMembers.begin(), group.groupMembers.end(),
                            [](const GroupMember& member) { return member.isOnline; })) ++onlineGroups;
        body["uptimeSeconds"] = picojson::value(state.uptimeSeconds);
        body["connectedClients"] = picojson::value(static_cast<double>(state.connectedClients));
        body["totalGroups"] = picojson::value(static_cast<double>(state.groups.size()));
        body["onlineGroups"] = picojson::value(static_cast<double>(onlineGroups));
    } else if (request.Type == EChatCmdType::AdminGroups) {
        query = "groups";
        picojson::array groups;
        for (const auto& group : state.groups) {
            picojson::array members;
            for (const auto& member : group.groupMembers)
                members.emplace_back(picojson::object{{"name", picojson::value(member.name)},
                    {"online", picojson::value(member.isOnline)}});
            groups.emplace_back(picojson::object{{"id", picojson::value(group.groupId)},
                {"leader", picojson::value(group.leaderName)}, {"members", picojson::value(members)}});
        }
        body["groups"] = picojson::value(groups);
    } else {
        const bool zones = request.Type == EChatCmdType::AdminZones;
        query = zones ? "zones" : "channels";
        picojson::array entries;
        for (const auto& entry : zones ? state.zones : state.channels)
            entries.emplace_back(picojson::object{{"name", picojson::value(entry.first)},
                {zones ? "connectedClients" : "subscribers", picojson::value(static_cast<double>(entry.second))}});
        body[query] = picojson::value(entries);
    }
    body["query"] = picojson::value(query);
    FDaraChatMsg reply;
    reply.Type = EChatCmdType::AdminInfo;
    reply.Sender = "Server";
    reply.Recipient = request.Sender;
    reply.Message = picojson::value(body).serialize();
    return reply;
}
} // namespace dara
