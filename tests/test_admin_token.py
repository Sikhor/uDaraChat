import base64
import hashlib
import hmac
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('admin_token', Path(__file__).resolve().parents[1] / 'tools/admin_token.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AdminTokenTests(unittest.TestCase):
    def test_signed_claims_and_lifetime(self):
        token = module.generate_admin_token('test-secret-ä', 'ChatAdmin', now=1792310400)
        header, payload, signature = token.split('.')
        decode = lambda part: base64.urlsafe_b64decode(part + '=' * (-len(part) % 4))
        claims = json.loads(decode(payload))
        self.assertEqual(json.loads(decode(header))['alg'], 'HS256')
        self.assertEqual(claims['roles'], ['chat_admin'])
        self.assertEqual(claims['characterName'], 'ChatAdmin')
        self.assertEqual(claims['iss'], 'dara-game-server')
        self.assertEqual(claims['aud'], 'dara-chat')
        self.assertEqual(claims['exp'] - claims['iat'], 900)
        self.assertEqual(decode(signature), hmac.new('test-secret-ä'.encode(),
                         f'{header}.{payload}'.encode(), hashlib.sha256).digest())

    def test_invalid_inputs(self):
        for secret, name, minutes in [('', 'Admin', 15), ('secret\n', 'Admin', 15),
                ('secret', 'Bad:Name', 15), ('secret', '', 15),
                ('secret', 'Admin', 0), ('secret', 'Admin', 61)]:
            with self.assertRaises(ValueError):
                module.generate_admin_token(secret, name, minutes)
