#include "AdminQueries.h"
#include <cassert>
#include <iostream>

int main() {
    dara::AdminSnapshot state;
    state.connectedClients = 3;
    state.uptimeSeconds = 42;
    GroupInfo group;
    group.groupId = "group_test";
    group.leaderName = "Alice\"";
    GroupMember member;
    member.name = group.leaderName;
    member.isOnline = true;
    group.groupMembers.push_back(member);
    state.groups.push_back(group);
    group.groupMembers[0].isOnline = false;
    state.groups.push_back(group);
    state.zones["Dungeon"] = 2;
    state.channels["World"] = 3;
    int collections = 0;
    auto collect = [&]() { ++collections; return state; };
    FDaraChatMsg request;
    request.Sender = "Admin";
    request.Type = EChatCmdType::AdminStatus;
    auto denied = dara::AnswerAdminQuery(request, false, collect);
    assert(denied.Type == EChatCmdType::AdminError && collections == 0);
    assert(denied.Message.find("Forbidden") != std::string::npos);
    request.Message = "unexpected";
    assert(dara::AnswerAdminQuery(request, true, collect).Type == EChatCmdType::AdminError);
    assert(collections == 0);
    request.Message.clear();
    for (auto type : {EChatCmdType::AdminStatus, EChatCmdType::AdminGroups,
                      EChatCmdType::AdminZones, EChatCmdType::AdminChannels}) {
        request.Type = type;
        const auto response = dara::AnswerAdminQuery(request, true, collect);
        assert(response.Type == EChatCmdType::AdminInfo && response.Sender == "Server");
        assert(response.Recipient == "Admin");
        picojson::value json;
        assert(picojson::parse(json, response.Message).empty());
        if (type == EChatCmdType::AdminStatus) {
            assert(json.get("onlineGroups").get<double>() == 1);
            assert(json.get("totalGroups").get<double>() == 2);
            assert(json.get("connectedClients").get<double>() == 3);
        } else if (type == EChatCmdType::AdminGroups) {
            const auto& groups = json.get("groups").get<picojson::array>();
            assert(groups.size() == 2 && groups[0].get("leader").get<std::string>() == "Alice\"");
        } else if (type == EChatCmdType::AdminZones) {
            assert(json.get("zones").get<picojson::array>()[0].get("connectedClients").get<double>() == 2);
        } else {
            assert(json.get("channels").get<picojson::array>()[0].get("subscribers").get<double>() == 3);
        }
        FDaraChatMsg parsed;
        std::string error;
        assert(uDaraChatLibrary().TryParseMessage(response.Serialize(), parsed, error));
        assert(parsed.Message == response.Message);
    }
    request.Type = EChatCmdType::AdminInfo;
    assert(dara::AnswerAdminQuery(request, true, collect).Type == EChatCmdType::AdminError);
    assert(collections == 4);
    std::cout << "Admin query checks passed\n";
}
