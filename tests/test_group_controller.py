import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('controller', ROOT / 'tools/group_test_controller.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        bot = self.folder / 'bot'
        bot.write_text('''#!/usr/bin/env python3
import os,sys,json
print(json.dumps({'secret_present':'DARA_CHAT_JWT_SECRET' in os.environ,
                  'secret_file_present':'DARA_CHAT_JWT_SECRET_FILE' in os.environ,
                  'token':os.environ['DARA_CHAT_JWT_TOKEN']}),flush=True)
print('CONTROL READY',flush=True)
for line in sys.stdin:
    print('COMMAND '+line.strip(),flush=True)
''')
        bot.chmod(0o700)
        self.args = SimpleNamespace(prefix='TestBot', days=1, delay=0.01,
                                    startup_timeout=1, bot=bot, log_dir=self.folder / 'logs',
                                    unreal_character='UnrealHero')
        self.issued = []

        def generate(secret, name, days):
            self.issued.append((secret, name, days))
            return 'private-token-' + name
        self.controller = module.Controller(self.args, 'signing-secret', generate)
        self.addCleanup(self.controller.close)

    def test_token_isolation_and_command_forwarding(self):
        with patch.dict(os.environ, {'DARA_CHAT_JWT_SECRET': 'signing-secret',
                                    'DARA_CHAT_JWT_SECRET_FILE': '/private/path'}):
            self.controller.start('TestBot01')
        self.controller.execute('invite TestBot01 UnrealHero')
        self.controller.execute('leave TestBot01')
        self.controller.close()
        content = (self.args.log_dir / 'TestBot01.log').read_text()
        self.assertIn('"secret_present": false', content)
        self.assertIn('"secret_file_present": false', content)
        self.assertNotIn('private-token-TestBot01', content)
        self.assertIn('COMMAND world Hey World! TestBot01 has arrived.', content)
        self.assertEqual(len(set(module.GREETINGS)), 6)
        self.assertIn('COMMAND invite UnrealHero', content)
        self.assertIn('COMMAND leave', content)
        self.assertEqual(self.issued, [('signing-secret', 'TestBot01', 1)])
        self.assertIsNotNone(self.controller.processes['TestBot01'].poll())

    def test_startup_failure_cleanup(self):
        self.args.bot.write_text('#!/usr/bin/env python3\nraise SystemExit(1)\n')
        with self.assertRaises(RuntimeError):
            self.controller.start('TestBot01')
        self.controller.close()
        self.assertIsNotNone(self.controller.processes['TestBot01'].poll())

    def test_scenario_capacity_and_targets(self):
        sent = []
        with patch.object(self.controller, 'reset'), patch.object(self.controller, 'send', side_effect=lambda *args: sent.append(args)), patch('builtins.input', return_value=''):
            self.controller.scenario('unreal')
        self.assertEqual(len(sent), 5)
        self.assertEqual(sent[0], ('TestBot01', 'invite UnrealHero'))
        self.assertNotIn(('TestBot01', 'invite TestBot06'), sent)
        with self.assertRaises(ValueError):
            self.controller.execute('invite UnknownBot UnrealHero')

    def test_six_processes_and_reconnect(self):
        for name in self.controller.names:
            self.controller.start(name)
        old = self.controller.processes['TestBot04']
        self.controller.execute('disconnect TestBot04')
        self.assertIsNotNone(old.poll())
        self.controller.execute('reconnect TestBot04')
        self.assertNotEqual(old.pid, self.controller.processes['TestBot04'].pid)
        self.assertEqual(len(self.issued), 7)
        self.controller.close()
        self.assertTrue(all(p.poll() is not None for p in self.controller.processes.values()))

    def test_group_greeting_waits_for_own_roster_and_deduplicates(self):
        c = self.controller
        c.start('TestBot01')
        roster = 'Received:GroupInfo:GroupAdmin::group_abc;TestBot01;1;2;0;TestBot01,1;TestBot02,1'
        with patch.object(c, 'send') as send:
            c.observe_group('TestBot02', roster)
            send.assert_not_called()
            c.observe_group('TestBot01', roster)
            send.assert_called_once_with('TestBot01', 'group Hey, I am here! TestBot01 is ready to rock!')
            c.observe_group('TestBot01', roster)
            self.assertEqual(send.call_count, 1)
            c.observe_group('TestBot01', 'CONTROL SENT leave ')
            c.observe_group('TestBot01', roster)
            self.assertEqual(send.call_count, 2)
        self.assertEqual(len(set(module.GROUP_GREETINGS)), 6)

    def test_group_display_and_roster_changes(self):
        import io
        c = self.controller
        c.observe_group('TestBot01', 'Received:GroupInfo:GroupAdmin::group_abc;TestBot01;1;2;0;TestBot01,1;TestBot02,1')
        self.assertEqual(c.groups['TestBot01'], 'group_abc')
        self.assertEqual(c.groups['TestBot02'], 'group_abc')
        self.assertIsNone(c.groups['TestBot03'])
        c.observe_group('TestBot01', 'Received:GroupDisbandInfo:TestBot02::left the group')
        self.assertEqual(c.groups['TestBot02'], '')
        c.observe_group('TestBot01', 'Received:GroupInfo:GroupAdmin::group_abc;TestBot01;1;1;0;TestBot01,1')
        self.assertEqual(c.groups['TestBot02'], '')
        c.observe_group('TestBot01', 'Received:GroupInfo:GroupAdmin::bad;leader;1;2;0;TestBot01,1')
        self.assertEqual(c.groups['TestBot01'], 'group_abc')
        out = io.StringIO()
        with patch('sys.stdout', out):
            c.execute('groups')
        self.assertIn('group_abc', out.getvalue())
        self.assertIn('none', out.getvalue())
        self.assertIn('unknown', out.getvalue())
        self.assertIn('stopped', out.getvalue())
        c.observe_group('TestBot01', 'CONTROL SENT leave ')
        self.assertIsNone(c.groups['TestBot01'])

    def test_actual_generator_claims(self):
        generator = module.load_generator(ROOT.parent / 'TokenGenerator/token_generator.py')
        import base64
        token = generator('test-secret', 'TestBot01', 1)
        part = token.split('.')[1]
        claims = json.loads(base64.urlsafe_b64decode(part + '=' * (-len(part) % 4)))
        self.assertEqual(claims['characterName'], 'TestBot01')
        self.assertEqual(claims['aud'], 'dara-chat')
        self.assertEqual(claims['exp'] - claims['iat'], 86400)
