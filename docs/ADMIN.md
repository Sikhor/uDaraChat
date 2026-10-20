# Remote chat server administration

The server supports read-only administrator queries over its existing WSS endpoint
(`wss://gameinfo.daraempire.com:9020/`). No additional port is needed.
The updated server must be rebuilt and restarted before these commands are available.

## Authorization and existing clients

The JWT signing secret, HS256 algorithm, issuer (`dara-game-server`), audience
(`dara-chat`), and existing required claims remain unchanged. Normal Unreal player
tokens without a `roles` claim still authenticate and have ordinary chat access.

To grant administrator access, a trusted issuer adds this signed JWT payload claim:

```json
"roles": ["chat_admin"]
```

`roles` must be an array of strings. Missing roles, an empty array, or other role
names do not grant administrator permission; malformed roles reject login.
A login parameter, character name, or chat message cannot grant permission.
Your game server must determine authorization from trusted account data, never
from a player-supplied role. Only administrators should have the signing secret.

Administrator commands require an unexpired administrator JWT, including on an
already-connected socket. After expiration they return `Forbidden`; reconnect
with a fresh token. Normal chat retains its existing connection authentication
lifetime. There is no in-place token refresh or role revocation mechanism.

## Start an external administrator console

The existing bot's `controlled` mode can act as an interactive administrator console.
Use a dedicated name such as `ChatAdmin`: logging in under an existing character
name disconnects its previous session. The console is included in client/zone counts.

On a trusted machine with access to the existing signing secret:

```sh
make uDaraChatBot
export DARA_CHAT_JWT_TOKEN="$(python3 tools/admin_token.py --name ChatAdmin)"
./uDaraChatBot --mode controlled --name ChatAdmin
unset DARA_CHAT_JWT_TOKEN
```

The helper uses `DARA_CHAT_JWT_SECRET` if set; otherwise it reads
`DARA_CHAT_JWT_SECRET_FILE` or the default `../secrets/DARA_CHAT_JWT_SECRET`.
Use `--secret-file /path/to/secret` to override the file. It does not change or
regenerate the secret. Tokens last 15 minutes by default; `--minutes` accepts
1 through 60. The helper prints a bearer token to stdout; the example captures
it in an environment variable rather than placing it in process arguments.

For an external machine without the secret, obtain a signed administrator token
from your trusted issuer and enter it without echoing it:

```sh
read -r -s -p 'Administrator JWT: ' DARA_CHAT_JWT_TOKEN
printf '\n'
export DARA_CHAT_JWT_TOKEN
./uDaraChatBot --mode controlled --name ChatAdmin
unset DARA_CHAT_JWT_TOKEN
```

The name must match the token's `characterName`. The console verifies TLS using
the system trust store, or the CA configured by `SSL_CERT_FILE`.
The token is not printed by the bot. A `CONTROL READY` message indicates the
connection and login have been sent; it is not a positive authentication response.
A successful administrator query confirms administrator access.

At the console, enter:

```text
admin-status
admin-groups
admin-zones
admin-channels
quit
```

Responses appear as `Received:AdminInfo:Server:ChatAdmin:<JSON>`.
The normal bot still accepts invitations automatically, so use a dedicated admin
identity if you want it to stay outside test groups.

## Queries and definitions

| Console command | Wire request | Result |
| --- | --- | --- |
| `admin-status` | `AdminStatus:ChatAdmin::` | Uptime in seconds, connected clients, total groups, online groups |
| `admin-groups` | `AdminGroups:ChatAdmin::` | Every group ID, leader, and member with online status |
| `admin-zones` | `AdminZones:ChatAdmin::` | Zones with connected clients and their counts |
| `admin-channels` | `AdminChannels:ChatAdmin::` | Currently subscribed channels and subscriber counts |

An online group has at least one online member. Total groups include one-member
and entirely offline groups. Offline members still count as group members.
Active zones reflect the zone supplied by connected clients at login, not a
server-verified game location; an empty zone name can appear if omitted at login.
Channels include `World` and custom channels, excluding internal `tell_`, `group_`,
and `zone_` topics. Channels with no connected subscribers do not appear.
Counts describe current authenticated sessions and their tracked subscriptions.

## Wire protocol and examples

Requests and replies use `Type:Sender:Recipient:Message`. The server replaces
request `Sender` with the authenticated identity. Requests require empty
`Recipient` and `Message`. Split at only the first three colons, then parse
`Message` as JSON for responses.

```text
AdminStatus:ChatAdmin::
AdminInfo:Server:ChatAdmin:{"query":"status","uptimeSeconds":120,"connectedClients":12,"totalGroups":4,"onlineGroups":3}

AdminGroups:ChatAdmin::
AdminInfo:Server:ChatAdmin:{"query":"groups","groups":[{"id":"group_123","leader":"Alice","members":[{"name":"Alice","online":true},{"name":"Bob","online":false}]}]}

AdminZones:ChatAdmin::
AdminInfo:Server:ChatAdmin:{"query":"zones","zones":[{"name":"Dungeon","connectedClients":2}]}

AdminChannels:ChatAdmin::
AdminInfo:Server:ChatAdmin:{"query":"channels","channels":[{"name":"World","subscribers":3}]}
```

JSON object field order is not significant. Empty lists are returned as `[]`.
Replies are sent directly to the requesting WebSocket, never published to a
channel or private topic. Do not trust administrator responses received through
ordinary channel broadcasts or the legacy `publish:` mechanism.

A normal player or an administrator with expired privileges receives:

```text
AdminError:Server:ChatAdmin:{"code":"Forbidden","message":"Administrator access required"}
```

Malformed query parameters and attempts to send response types as requests return
`AdminError` with code `InvalidQuery` to administrators. Invalid/unknown message
types are handled by the normal protocol parser and discarded.

The legacy `Tell` message whose content begins with `DEBUG` now also requires
unexpired administrator permission. It writes diagnostics to server logs;
the queries above return structured results to the console instead.
