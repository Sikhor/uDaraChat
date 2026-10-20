#include "ChatLibrary.h"
#include <cassert>
#include <iostream>

int main() {
    uDaraChatLibrary library;
    FDaraChatMsg msg;
    std::string error;
    for (const auto type : {EChatCmdType::Tell, EChatCmdType::Group,
            EChatCmdType::Raid, EChatCmdType::Zone, EChatCmdType::Channel,
            EChatCmdType::GroupInvite, EChatCmdType::GroupJoin,
            EChatCmdType::GroupJoinInfo, EChatCmdType::GroupDisband,
            EChatCmdType::GroupDisbandInfo, EChatCmdType::GroupKick,
            EChatCmdType::GroupKickInfo, EChatCmdType::GroupInfo,
            EChatCmdType::Alive, EChatCmdType::AliveInfo, EChatCmdType::AdminStatus,
            EChatCmdType::AdminGroups, EChatCmdType::AdminZones, EChatCmdType::AdminChannels,
            EChatCmdType::AdminInfo, EChatCmdType::AdminError}) {
        const std::string frame = ChatCmdTypeToString(type) + ":Alice::text:with:colons::";
        assert(library.TryParseMessage(frame, msg, error));
        assert(msg.Type == type);
        assert(msg.Sender == "Alice" && msg.Recipient.empty());
        assert(msg.Message == "text:with:colons::");
        assert(msg.Serialize() == frame);
    }
    assert(library.TryParseMessage("GroupInvite:Alice:Bob:", msg, error));
    assert(msg.Recipient == "Bob" && msg.Message.empty());
    assert(library.TryParseMessage("Tell:Alice:Bob:Hello", msg, error));
    assert(msg.Serialize() == "Tell:Alice:Bob:Hello");
    assert(library.TryParseMessage("Group:Alice::", msg, error));
    for (const auto* invalid : {"", "Tell:Alice:Bob", "Unknown:Alice::",
            "None:Alice::", "Cmd:GroupInvite:Alice:Bob:", "group:Alice::"}) {
        assert(!library.TryParseMessage(invalid, msg, error));
        assert(!error.empty() && msg.Type == EChatCmdType::None);
    }
    msg.Sender = "Alice";
    assert(library.TryParseMessage(library.GetGroupDisbandInfoMessage(msg), msg, error));
    assert(msg.Type == EChatCmdType::GroupDisbandInfo);
    assert(msg.Sender == "Alice" && msg.Recipient.empty());
    assert(msg.Message == "left the group");
    std::cout << "Chat protocol tests passed\n";
}
