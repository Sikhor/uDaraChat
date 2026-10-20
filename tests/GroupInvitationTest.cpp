#include "GroupContainer.h"
#include <cassert>
#include <iostream>

int main() {
    GroupContainer groups;
    const auto oldGroup = groups.GroupStartGroup("Solo").groupId;
    const auto destination = groups.GroupStartGroup("Leader").groupId;
    groups.AddInvite("Solo", "OldInvitee");
    assert(groups.GroupInvite("Solo", "Leader").errorCode == 1);
    // Receiving or ignoring an invitation must not destroy the solo group.
    assert(groups.GetGroupInfo("Solo").groupId == oldGroup);
    assert(groups.CheckHasInvited("Solo", "OldInvitee"));
    groups.AddInvite("Leader", "Solo");
    assert(groups.CheckHasInvited("Leader", "Solo"));
    groups.GroupDisband("Solo");
    assert(groups.GetGroupInfoByGroupId(oldGroup).memberCount == 0);
    assert(!groups.CheckHasInvited("Solo", "OldInvitee"));
    assert(groups.CheckHasInvited("Leader", "Solo"));
    const auto joined = groups.GroupJoin("Solo", "Leader");
    assert(joined.errorCode == 0 && joined.groupId == destination);
    assert(groups.GetGroupInfo("Solo").memberCount == 2);
    assert(groups.GroupInvite("Leader", "Leader").errorCode != 1);
    assert(groups.GroupInvite("Solo", "Leader").errorCode != 1);

    groups.GroupStartGroup("OtherLeader");
    // A member of a group with other members cannot be invited away.
    assert(groups.GroupInvite("Solo", "OtherLeader").errorCode != 1);
    assert(groups.GetGroupInfo("Solo").groupId == destination);
    assert(groups.GroupInvite("NewPlayer", "Solo").errorCode != 1);
    assert(groups.GroupInvite("NewPlayer", "OtherLeader").errorCode == 1);
    std::cout << "Group invitation checks passed\n";
}
