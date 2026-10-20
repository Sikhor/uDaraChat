# Group test controller

The controller is a separate Python 3 program that launches six independent
`uDaraChatBot` processes. Each bot uses its own character name, JWT and WSS
connection to `gameinfo.daraempire.com:9020`. No Python packages are required.

## Start

From the `uDaraChat` directory:

```sh
make uDaraChatBot
python3 tools/group_test_controller.py --unreal-character YourCharacter
```

Use the exact name of your Unreal character. Log into Unreal normally; its token
continues to come from the game login flow. Use an isolated test environment or
names that no real player uses: the server replaces an existing connection when
another client logs in with the same character name.

The controller reads `DARA_CHAT_JWT_SECRET` from its environment or the existing
`../secrets/DARA_CHAT_JWT_SECRET` file. Override the file with `--secret-file PATH`
or `DARA_CHAT_JWT_SECRET_FILE`. It imports `../TokenGenerator/token_generator.py`
to generate one-day HS256 tokens. These defaults resolve relative to the project,
not the working directory. Override the generator with `--token-generator PATH`.

The signing secret must match the chat server's secret. Only the controller needs
it: the signing-secret environment variables are removed from child environments,
and each bot gets only its own JWT through `DARA_CHAT_JWT_TOKEN`. Tokens and the
signing secret are not placed on process command lines or printed by the controller.
This is a trusted administrator tool capable of minting character identities.

Each bot sends its own distinct arrival greeting to World after startup (and again
on reconnect), including its character name. Unreal must display the World channel
to see these messages.

The default bot names are `TestBot01` through `TestBot06`. Use `--prefix AnotherBot`
to change them. `--days 1` controls validity; `--delay 2` changes the pause between
commands. `--bot PATH`, `--startup-timeout 15` and `--log-dir PATH` are also available.
Run `python3 tools/group_test_controller.py --help` for all options.

## Scenarios

At the `group-test>` prompt, enter one of:

```text
scenario bots
scenario unreal
scenario unreal-led
```

- `bots`: all running bots leave their previous groups, then TestBot01 invites
  TestBot02–06. Bots automatically accept invitations. This fills the six-member
  group. Unreal is outside it and should not receive its private roster updates.
- `unreal`: bots leave their previous groups; the controller asks you to ensure
  Unreal is connected and outside any old group. Press Enter to invite your Unreal
  character from TestBot01. Accept in Unreal, then press Enter again. TestBot01
  invites TestBot02–05, filling the group with five bots plus Unreal. TestBot06
  remains outside for capacity/invalid invitation tests.
- `unreal-led`: bots leave their groups, then the controller prints instructions.
  In Unreal, leave any old group and invite TestBot01–05. They accept automatically.
  This tests Unreal's invitation and leadership UI.

Run scenarios with all six bots connected. `reset` only makes **running bots**
leave; it cannot make the Unreal character leave or remove offline bots. Leave
from Unreal and reconnect offline bots before switching scenarios if necessary.

## Manual actions

```text
invite TestBot01 TestBot06
kick TestBot01 TestBot03
leave TestBot02
disconnect TestBot04
reconnect TestBot04
groups
status
reset
quit
```

The first argument is the bot performing the action. Invitation and kick targets
can be bots or your Unreal character. A kick is subject to the server's normal
leader checks. `reconnect` stops the old process if necessary, generates a fresh
JWT and starts the same identity again. `disconnect` intentionally leaves group
membership in place so you can inspect offline presence in Unreal.

The current protocol's `GroupDisband` command removes the requesting member and
assigns a new leader when required. The controller therefore exposes it as `leave`,
not as an action that dissolves the entire group.

Watch Unreal's roster while you:

1. Invite members and check names, count and leader.
2. Disconnect/reconnect a bot and check online status and preserved membership.
3. Make the leader leave and check leader reassignment.
4. Invite TestBot06 to a full Unreal group and check capacity handling.
5. Repeat invitations or request an unauthorized kick and check that membership
   remains consistent.

These are commands for interactive testing, not automated success assertions.
The current server sends no explicit login acknowledgement. `CONTROL READY` means
WSS opened and the bot sent login/subscriptions; an immediate disconnect is treated
as startup failure, but a running process alone does not prove authentication.
Confirm actual group membership through received messages and the Unreal UI.

## Logs and cleanup

Each bot's complete output goes to `log/group-test-<timestamp>/TestBotNN.log`.
Received messages, group updates and connection failures are also printed with a
bot-name prefix in the controller terminal. `status` reports process state only.
New log files are created with mode 0600. Tokens are redacted from collected output.

`quit`, Ctrl+C, terminal EOF and normal error exits stop the child processes.
Stopping bots does not erase their server-side groups: run `reset` while they are
connected, and leave the group in Unreal, if you want to clear membership first.

A bot can also be controlled directly using `--mode controlled` with its normal
`--name` and JWT options. Its stdin accepts `invite NAME`, `kick NAME`, `leave`
and `quit`; EOF stops it. Existing bot modes remain available.

## Verification

```sh
make test
python3 -m unittest discover -s tests -p 'test_group_controller.py' -v
```

Controller tests use local fake bot processes, not the deployed game/chat server.
They check token isolation, command forwarding, child cleanup and startup failure
handling. Live WSS/group behavior requires your running server and Unreal client.

## Show bot group IDs

Enter `groups` at the controller prompt to list every bot's last reported group
ID alongside its running/stopped process state. Membership is tracked from received
server `GroupInfo` rosters and departure messages. `none` means a roster or departure
update showed the bot leaving its known group; `unknown` means no confirmed update
is available (including after sending a leave request). A stopped bot can still
belong to its cached group. This command displays cached observations; it does not
query the server or assume that sending an invitation succeeded.
