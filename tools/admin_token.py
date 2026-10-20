#!/usr/bin/env python3
"""Issue a short-lived chat administrator JWT using the server's existing secret."""
import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]


def generate_admin_token(secret, name, minutes=15, now=None):
    if not secret or '\n' in secret or '\r' in secret:
        raise ValueError('Signing secret must be one nonempty line')
    if not name or any(c.isspace() or c in ':;\0' for c in name):
        raise ValueError('Name must be nonempty without whitespace, colons or semicolons')
    if not 1 <= minutes <= 60:
        raise ValueError('Validity must be between 1 and 60 minutes')
    issued = int(time.time()) if now is None else now
    payload = dict(iss='dara-game-server', aud='dara-chat', characterName=name,
                   iat=issued, exp=issued + minutes * 60, roles=['chat_admin'])

    def encode(value):
        return base64.urlsafe_b64encode(value).rstrip(b'=')
    message = b'.'.join(encode(json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))
                        for value in ({'alg': 'HS256', 'typ': 'JWT'}, payload))
    signature = hmac.new(secret.encode('utf-8'), message, hashlib.sha256).digest()
    return (message + b'.' + encode(signature)).decode('ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', default='ChatAdmin')
    parser.add_argument('--minutes', type=int, default=15)
    parser.add_argument('--secret-file', type=Path, default=Path(os.environ.get(
        'DARA_CHAT_JWT_SECRET_FILE', ROOT.parent / 'secrets/DARA_CHAT_JWT_SECRET')))
    args = parser.parse_args()
    try:
        secret = os.environ.get('DARA_CHAT_JWT_SECRET')
        if secret is None:
            secret = args.secret_file.read_text(encoding='utf-8').rstrip('\n')
        print(generate_admin_token(secret, args.name, args.minutes))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Unable to issue administrator token: {error}\n')


if __name__ == '__main__':
    main()
