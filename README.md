# uDaraChat
Game chat WebSocket server based on the uWebSockets library.

The [interface documentation](INTERFACE.md) covers authentication,
chat messages, channels, group management, and status updates, with message examples.

Authentication requires `DARA_CHAT_JWT_SECRET` in the server environment, matching
the Unreal game server's signing secret exactly as raw UTF-8 bytes. Inject it
through your deployment's secret configuration; never commit or log it. Only
HS256 game-server JWTs are accepted. Token expiry is checked at login and
reconnect; established connections stay authenticated after token expiry. No
refresh is currently required or implemented. Use WSS with a trusted production certificate
(`/etc/letsencrypt/live/gameinfo.daraempire.com/privkey.pem` and `fullchain.pem`).
Build uSockets with TLS enabled (`make -C uWebSockets/uSockets WITH_OPENSSL=1`).

Run `make uDaraChatServer` to build and `make test` for authentication tests.
JWT verification uses vendored [jwt-cpp v0.7.2](https://github.com/Thalhammer/jwt-cpp/tree/v0.7.2)
(commit `b0ea29a58fc852a67d4e896d266880c2c63b0c4c`), including its picojson dependency
and MIT license under `third_party/jwt-cpp`. OpenSSL development headers and
libraries are required. Update the pinned vendor copy when upgrading jwt-cpp.

The legacy sample clients under `client/` still generate SHA-1 credentials and
are incompatible with JWT login; game clients must supply Unreal-issued tokens.

## Local secret file

`make run` and `make StartServer` load the JWT secret from
`/home/sikhor/source/projects/secrets/DARA_CHAT_JWT_SECRET` (the `secrets`
directory beside this repository). This file is outside the Git repository.
Keep the directory private (`chmod 700`) and the file private (`chmod 600`).

Edit that file and put the exact Unreal signing secret on a single line:
raw UTF-8 text, without quotes, `export`, or a `DARA_CHAT_JWT_SECRET=` prefix.
One trailing newline is allowed. The file is read as data, never sourced as
shell code, so special characters such as `$` remain literal.

Then start the server with `make StartServer` or `make run`. The launcher exports
`DARA_CHAT_JWT_SECRET` only for the launched process and its children. An empty
or unreadable file prevents startup. Restart an existing server after changing
the file. To use another file, set `DARA_CHAT_JWT_SECRET_FILE` to its path.
Directly running `./uDaraChatServer` still requires an exported environment variable.

## JWT chatbot

Build with `make uDaraChatBot`. Pass a token using `--token`:

```sh
./uDaraChatBot --mode silent --name Alice --token "$TOKEN" --sleep 5
```

`--name` must exactly match the token's `characterName`. The bot sends the token
in the login `pass` field; it does not generate tokens or need the signing secret.
Obtain a fresh token before reconnecting if the previous token has expired.
Alternatively, set `DARA_CHAT_JWT_TOKEN` and omit `--token`. The command-line
option takes precedence. Tokens are not printed.

The bot connects to `wss://gameinfo.daraempire.com:9020/`, resolves the hostname
using DNS, sends TLS SNI, and verifies the server certificate against that hostname
and the system trust store. The certificate must cover `gameinfo.daraempire.com`. For a private CA, configure OpenSSL's
`SSL_CERT_FILE` with the trusted CA certificate. TLS verification is required.

## Server certificate access

The server loads the Let's Encrypt `privkey.pem` and `fullchain.pem` directly
from `/etc/letsencrypt/live/gameinfo.daraempire.com/`. Missing or unreadable files,
invalid TLS configuration, and failure to bind port 9020 stop startup with an error.

Run the server as a dedicated service user. Grant a dedicated certificate-reader
group read access to the private key (mode 0640) and grant that group traversal
access to every parent directory of both the live link and its archive target.
The service user must belong to that group; restart its service after changing
group membership. Do not make the private key world-readable.

Use `namei -l /etc/letsencrypt/live/gameinfo.daraempire.com/privkey.pem` on the
server to inspect permissions along the symlink path without displaying key data.
Certbot preserves private-key group ownership and group mode on renewal. Use a
Certbot deploy hook to restart the chat service after successful renewal: this
server loads certificates only at startup. Any directory access rules must cover
future archive files as well as the current ones.

## Group testing with bots and Unreal

Use the [group test controller guide](docs/GROUP_TEST_CONTROLLER.md) to start six
JWT-authenticated bots, run group invitation scenarios with your Unreal character,
and test leave, kick, disconnect and reconnect behavior:

```sh
python3 tools/group_test_controller.py --unreal-character YourCharacter
```
