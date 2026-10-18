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
(`misc/key.pem` and `misc/cert.pem` in the current TLS configuration).
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
