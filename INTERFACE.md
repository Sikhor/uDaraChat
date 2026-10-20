# uDaraChatServer – Interface Documentation

As of October 20, 2026, based on the current local source code.
This document describes the behavior actually implemented by the server.

## 1. Connection and authentication

The server exposes a WebSocket interface over TLS (`wss`) on port **9020**.
All WebSocket paths are accepted (`/*`), for example `wss://<server>:9020/`.
`ChatLibrary.h` contains the address `85.215.47.227:9020`; reachability and
certificates have not been verified here. The server configures the TLS files
as `misc/key.pem` and `misc/cert.pem`.

After the WebSocket handshake, the client must first send a text frame:

```text
connect:name=Alice;zone=Dungeon;group=;pass=<JWT>
```

| Parameter | Meaning |
| --- | --- |
| `name` | Character name; always use the same capitalization for consistent group management |
| `zone` | Current zone, for example `Dungeon` |
| `pass` | JWT issued by the Unreal game server |
| `group` | Ignored; the server determines group membership itself |

Parameters may appear in any order; duplicate fields are rejected. The JWT must
be signed with **HS256** using `DARA_CHAT_JWT_SECRET` as raw UTF-8 bytes (no Base64
or hex decoding). The secret must exactly match the Unreal server's value.
The server refuses to start if it is missing or empty. Other algorithms,
including `none`, and old hashed passwords are rejected.

Required claims are `iss` = `dara-game-server`, `aud` containing `dara-chat`,
integer Unix timestamps `iat` and `exp`, and a nonempty string `characterName`.
Expired tokens and future-issued tokens are rejected with at most 30 seconds
of clock tolerance. `exp` must be later than `iat`. Unreal currently issues
1800-second tokens; the chat server does not impose an additional lifetime cap.
The message's `name` must exactly match the verified `characterName`, which is
used as the authenticated identity. `sub` is not used because it is currently
`"0"` for all players. JWT authentication does **not** verify the supplied `zone`
or `group`; `group` is ignored in favor of server-managed membership.

Invalid logins close the WebSocket with code `1008`, reason `Authentication failed`.
Server logs distinguish `[WS OPEN]` (transport connected, awaiting login),
`[AUTH REJECTED]` (with IP and a safe validation reason), and `[AUTH REQUIRED]`
(a message arrived before successful authentication). Rejection reasons identify
missing/malformed tokens, unsupported algorithms, invalid claims, clock/expiry
problems, signature failures, and character-name mismatches without logging
credentials. Claim diagnostics describe validation failures, not trusted identity.
There is no explicit success response. Raw text frames, JWTs, and signing secrets
are not logged. Use **WSS in production**: the JWT is a bearer credential.

Every new connection verifies its JWT again, including expiration. Once a
connection is authenticated, it stays authenticated until it closes, even after
the JWT expires. There is currently no token refresh requirement or refresh
protocol. Refresh support may be added later. Reconnecting requires a JWT that
is valid at the time of login; an expired token cannot authenticate a new
connection.

Sending `connect:` on an already authenticated socket closes it with code `1008`,
reason `Reconnect to reauthenticate`. To authenticate again, open a new WebSocket.

Successful authentication automatically subscribes the client to these topics:

```text
World
zone_<zone>
tell_<lowercase-name>
group_<uuid>                   # if already a group member
```

A new authentication using the exact same character name closes the old connection
with code `1000` and reason `Reconnected`.
Groups survive disconnections; the member is marked offline. On reconnection,
the client subscribes to its group again. Groups and invitations exist only in
memory and are lost when the server restarts.

## 2. Message format

### Client → Server

Each chat message or group command is sent as a separate WebSocket text frame:

```text
Type:Sender:Recipient:Message
```

| Field | Meaning |
| --- | --- |
| `Type` | One `EChatCmdType` value: chat type, group command, or notification |
| `Sender` | Overwritten by the server with the authenticated character name |
| `Recipient` | Target character or channel; empty for group and zone operations |
| `Message` | Message text or command-specific content; may be empty |

Types are case-sensitive. Both directions use the same four fields. Split at
only the first three colons; the remaining text is preserved exactly, including
trailing colons. Empty messages require the final delimiter. Unknown types,
`None`, and malformed envelopes are rejected. Sender and recipient cannot contain
colons. Avoid `:`, `;`, and `,` in names and zones because payloads also use separators.

Group and zone destinations are determined from the authenticated connection;
clients need not send group IDs or zone names. Their recipient fields are ignored.
The server retains the recipient when forwarding messages, so custom channels
can be identified. Server-generated group notifications use an empty recipient.

This format replaces the previous protocol and requires updated clients.
Authentication, subscriptions, and binary frames keep their existing formats.

## 3. Chat messages and channels

| Purpose | Example Client → Server | Actual destination |
| --- | --- | --- |
| World chat | `Channel:Alice:World:Hello everyone!` | Topic `World` |
| Custom channel | `Channel:Alice:Trade:Selling a sword` | Topic `Trade` |
| Zone chat | `Zone:Alice::Who is here?` | `zone_` + zone supplied during authentication |
| Group chat | `Group:Alice::Ready?` | Sender's current group |
| Private message | `Tell:Alice:Bob:Hello Bob!` | Topic `tell_bob` |

Example of a private message received by Bob:

```text
Tell:Alice:Bob:Hello Bob!
```

For `Channel`, the recipient is used **unchanged as the topic**; no `channel_`
prefix is added. For `Zone` and `Group`, `Recipient` is ignored.
Group chat is discarded if the sender is not in a group.
`Tell` converts only the target name to lowercase.
Messages sent to offline recipients are not stored.
Messages are published to all current subscribers, including the sender if it
subscribes to the destination topic. The sender does not automatically receive
an echo of a private message.

### Subscribing to and leaving custom channels

```text
subscribe:Trade
unsubscribe:Trade
```

A custom channel name must contain at least one character and may contain only
`A–Z`, `a–z`, `0–9`, and `_`. Names are case-sensitive.
For `subscribe:`, `World` and the prefixes `tell_`, `group_`, and `zone_` are
reserved and rejected; these topics are managed automatically.
`subscribe:Dungeon` would subscribe to a custom channel, **not** the zone chat
`zone_Dungeon`. Each connection has 32 managed topic slots, including those used
by automatic topics. There are no subscription acknowledgement messages.

For `Channel`, the current server does not check whether the sender subscribes
to the destination or whether it is a reserved topic. `unsubscribe:` can also
remove automatic topics. Clients should use reserved topics only through their
intended chat types and retain automatically managed subscriptions.

There is no dedicated command for changing zones. To change zones, authenticate
a new WebSocket connection with the new zone. Sending `connect:` again on the
same authenticated connection closes it; use a new connection.

## 4. Managing groups

A group can contain at most **6 members**, including offline members.
The group ID has the form `group_<uuid>` and also serves as its topic.

### Inviting a member

Alice sends:

```text
GroupInvite:Alice:Bob:
```

If Alice is not already in a group, the server creates one with Alice as its
leader and subscribes her to its topic. In existing groups, only the leader can
invite members. The invited character may be ungrouped or in a one-member group. Characters
in groups with multiple members cannot be invited into another group. Bob receives this message through his private channel:

```text
GroupInvite:Alice:Bob:
```

The invitation is stored on the server and expires when it is more than
180 seconds old. Expired invitations are removed during invite/join operations.
There is no explicit rejection or cancellation message; an invitation may be ignored.
An invitation does not reserve a slot; capacity is checked when the character joins.

### Accepting an invitation

Bob uses the **inviter's name** as the recipient, rather than a group ID:

```text
GroupJoin:Bob:Alice:
```

The server checks the stored invitation, the existence of the target group, and
its capacity. On success, Bob automatically subscribes to the group topic, the
invitation is removed, and `GroupInfo` is sent to the entire group.
A character belonging to another group with more than one member cannot join.
A character's own one-person group can be left automatically during joining if
a valid invitation already exists. The old solo group remains intact until acceptance; removing it also cancels
invitations issued from that old group.

### Leaving a group

```text
GroupDisband:Bob::
```

**`GroupDisband` removes only the sender from the group.** It does not dissolve
an entire group with multiple members. If the leader leaves, the first remaining
member in the internal ordering becomes leader, regardless of online status.
After the last member leaves, no membership remains for that group.

The remaining subscribers first receive this regular notification:

```text
GroupDisbandInfo:Bob::left the group
```

A regular `GroupInfo` message follows. Although the code defines a
`GroupDisbandInfo` type, it is not included in the departure notification.
The departing client has already been unsubscribed before publication and does
not receive its own departure acknowledgement; its UI must account for this.

### Receiving group information

```text
GroupInfo:GroupAdmin::group_<uuid>;Alice;1;2;0;Alice,1;Bob,1
```

The content after `GroupInfo:GroupAdmin::` has this structure:

```text
groupId;leaderName;isLeader;memberCount;errorCode;name,online;name,online;...
```

| Field | Meaning |
| --- | --- |
| `groupId` | Group ID including `group_` |
| `leaderName` | Current leader's name |
| `isLeader` | In a group broadcast, `1` if a leader exists; not a recipient-specific role flag |
| `memberCount` | Number of all members, including offline members |
| `errorCode` | `0` in regular group broadcasts; not a general command result |
| `name,online` | Member name and online status: `1` online, `0` offline |

Determine whether the local character is leader using `leaderName == ownName`.
`GroupInfo` does not contain health values, zones, or positions.
The group-info payload deserializer also supports legacy member entries without `,online`;
these are read as offline by default.

`GroupInfo` is published after joining, leaving, reconnecting to an existing
group, and when a member disconnects. It is not sent when an invitation first
creates a group. Sending `GroupInfo:...` from a client is not an implemented
query command.

### Complete example with two clients

```text
Alice → Server: connect:name=Alice;zone=Dungeon;group=;pass=<JWT>
Bob   → Server: connect:name=Bob;zone=Dungeon;pass=<JWT>
Alice → Server: GroupInvite:Alice:Bob:
Server → Bob:  GroupInvite:Alice:Bob:
Bob   → Server: GroupJoin:Bob:Alice:
Server → both: GroupInfo:GroupAdmin::group_<uuid>;Alice;1;2;0;Alice,1;Bob,1
Alice → Server: Group:Alice::Let's go!
Server → both: Group:Alice::Let's go!
Bob   → Server: GroupDisband:Bob::
Server → Alice: GroupDisbandInfo:Bob::left the group
Server → Alice: GroupInfo:GroupAdmin::group_<uuid>;Alice;1;1;0;Alice,1
```

## 5. Alive and state updates

### Text message

```text
Alive:Alice::0.8
```

The server stores the content as a health string, updates the zone from the
authentication data, and marks the group member online. The group receives:

```text
AliveInfo:Alice::Dungeon;0.8
```

The text value is neither validated numerically nor converted. Clients can use
`0.8` to represent 80%, matching the binary calculation `Health / MaxHealth`.
The example bot sometimes uses `80`; the server does not enforce a consistent
scale. The UI should establish a shared scale.
There is no required Alive interval, and missing Alive messages do not remove
members from groups. The WebSocket close handler manages offline status.

### Binary frames: current implementation

Binary state updates are processed only for authenticated group members.
After successful validation, the original bytes are forwarded to the group,
and `Health / MaxHealth` is stored internally. `MaxHealth` must be positive.

| Byte offset | Field |
| --- | --- |
| 0–3 | ASCII signature `DARA` |
| 4, 8, 12 | Position X, Y, Z, each a `float` |
| 16, 20 | Health, MaxHealth |
| 24, 28 | Mana, MaxMana |
| 32, 36 | Energy, MaxEnergy |
| From 40 | Zone and CharName strings expected by the deserializer, each with an `int32` length prefix |

Numbers are processed as native memory representations without a defined network
byte order. **The binary format is currently inconsistent:** `Serialize()` produces
only 40 bytes and omits both strings, while `Deserialize()` requires them.
The string reader also checks lengths using `sizeof(wchar_t)` but reads and
advances the offset by only the specified byte count. The existing serializer is
therefore not a working counterpart to the server's reader. For integration,
use text `Alive` initially; the binary format needs a separate fix and an explicit
definition of string encoding and byte order.

## 6. Error behavior and additional commands

Invalid authentication, missing invitations, full groups, and other rejected
actions normally produce **no structured error response**. Internal negative
`GroupContainer` error codes are not sent as separate responses to clients.
Clients must therefore not interpret the absence of a response as success.
A successful join can be detected through the subsequent `GroupInfo` message.

| Feature | Current status |
| --- | --- |
| `GroupKick` / `GroupKickInfo` | Present in the library/container, but no WebSocket handler in the server |
| `Raid` | Type defined, but no implemented raid routing or management |
| `GroupJoinInfo` | No join event sent; `GroupInfo` is used instead |
| `schedule:` | Immediately ignored by the server |
| `Tell` whose text begins with `DEBUG` | Writes diagnostics to the server log; not delivered |
| `publish:<topic>:<content>` | Legacy path: publishes `<topic>:<content>` as a raw payload without a regular chat envelope |

The legacy `publish:` path is accessible to authenticated clients and does not
validate the destination topic. New clients should use the documented chat types.
Regular channel routing also does not enforce access permissions for private topics.

The server configures a maximum frame size of 16 MiB and 16 MiB of backpressure,
disables compression, and uses a 60-second idle timeout with automatic WebSocket
pings. These are transport parameters; the application protocol does not define
a separate maximum chat text length.

## 7. Small browser example

This example authenticates Alice, subscribes to `Trade`, and sends a world chat
message. The address and certificates must match the installation.
Browsers respond to WebSocket pings automatically.

```javascript
async function startChat(serverUrl, name, zone, jwt) {
  const ws = new WebSocket(serverUrl);
  ws.binaryType = "arraybuffer";
  ws.addEventListener("message", ({ data }) => {
    if (typeof data !== "string") {
      console.log("Binary group data", data);
      return;
    }
    const match = /^([^:]*):([^:]*):([^:]*):([\s\S]*)$/.exec(data);
    if (!match) return;
    const [, type, sender, recipient, message] = match;
    console.log({ type, sender, recipient, message });
    // For GroupInfo: split message at ';', then read members from field 5 (zero-based).
  });
  ws.addEventListener("open", () => {
    ws.send(`connect:name=${name};zone=${zone};pass=${jwt}`);
    // Frames are processed in order; there is no authentication ACK.
    ws.send("subscribe:Trade");
    ws.send(`Channel:${name}:World:Hello everyone!`);
  });
  return ws;
}

const ws = await startChat("wss://<server>:9020/", "Alice", "Dungeon", jwtFromGameServer);
// After the connection opens, for example:
// ws.send("Tell:Alice:Bob:Hello!");
// ws.send("GroupInvite:Alice:Bob:");
// ws.send("Group:Alice::Ready?");
```

## Project sources

- [uDaraChatServer.cpp](uDaraChatServer.cpp): Authentication, routing, subscriptions, and WebSocket events.
- [ChatLibrary.cpp](ChatLibrary.cpp): Text parsers, serialization, and binary state data.
- [GroupContainer.cpp](GroupContainer.cpp): Groups, invitations, online status, and GroupInfo.
- [ChatLibrary.h](ChatLibrary.h): Message types, structures, and configured server address.
- [Chatter.cpp](Chatter.cpp): Example bot messages.

## Administrator queries

`AdminStatus`, `AdminGroups`, `AdminZones`, and `AdminChannels` require the signed
`chat_admin` JWT role and return direct `AdminInfo` or `AdminError` responses.
See [Remote administration](docs/ADMIN.md) for authorization, examples, and console usage.
The legacy `Tell`/`DEBUG` diagnostic also requires administrator permission.
