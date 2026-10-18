#!/bin/sh
# Read a raw secret as data, never as shell code.
set +x
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
secret_file=${DARA_CHAT_JWT_SECRET_FILE:-"$script_dir/../../secrets/DARA_CHAT_JWT_SECRET"}
if [ ! -r "$secret_file" ]; then
    echo "Cannot read JWT secret file: $secret_file" >&2
    exit 1
fi
DARA_CHAT_JWT_SECRET=$(cat -- "$secret_file") || exit 1
case "$DARA_CHAT_JWT_SECRET" in
    ''|*'
'*)
        echo "JWT secret file must contain one nonempty line of raw UTF-8 text" >&2
        exit 1
        ;;
esac
export DARA_CHAT_JWT_SECRET
exec "$@"
