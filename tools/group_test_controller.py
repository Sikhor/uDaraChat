#!/usr/bin/env python3
"""Start JWT-authenticated test bots and control group scenarios."""
import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
GREETINGS = (
    "Hey World! {name} has arrived. Adventure boots on, ready for a party!",
    "Greetings, adventurers! {name} reporting for duty. Who ordered backup?",
    "Make way for {name}! Freshly spawned and already looking for trouble.",
    "Hello out there! {name} is online. My inventory is empty, my enthusiasm is full!",
    "Ta-da! {name} has entered the realm. Let the group adventures begin!",
    "World, meet {name}! Last to arrive, first to volunteer for questionable quests.",
)


def load_generator(path):
    spec = importlib.util.spec_from_file_location('dara_token_generator', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.generate_token


class Controller:
    def __init__(self, args, secret, generate):
        self.args, self.secret, self.generate = args, secret, generate
        self.names = [f'{args.prefix}{i:02d}' for i in range(1, 7)]
        self.processes = {}
        self.logs = {}
        self.ready = {}
        self.threads = []
        self.group_lock = threading.Lock()
        self.groups = {name: None for name in self.names}
        args.log_dir.mkdir(parents=True, exist_ok=True, mode=0o700)

    def start(self, name):
        if name in self.processes and self.processes[name].poll() is None:
            raise ValueError(f'{name} is already running')
        token = self.generate(self.secret, name, self.args.days)
        env = dict(os.environ)
        # Never pass the signing secret to child bots.
        env.pop('DARA_CHAT_JWT_SECRET', None)
        env.pop('DARA_CHAT_JWT_SECRET_FILE', None)
        env['DARA_CHAT_JWT_TOKEN'] = token
        fd = os.open(self.args.log_dir / f'{name}.log', os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        log = os.fdopen(fd, 'a', buffering=1)
        ready = threading.Event()
        try:
            proc = subprocess.Popen([str(self.args.bot), '--mode', 'controlled', '--name', name],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, bufsize=1, env=env)
        except Exception:
            log.close()
            raise
        self.processes[name], self.logs[name], self.ready[name] = proc, log, ready

        def collect():
            try:
                for line in proc.stdout:
                    line = line.replace(token, '[TOKEN REDACTED]')
                    log.write(line)
                    self.observe_group(name, line.strip())
                    if line.strip() == 'CONTROL READY':
                        ready.set()
                    if 'Received:' in line or 'failed' in line or 'disconnected' in line:
                        print(f'[{name}] {line.rstrip()}', flush=True)
            finally:
                proc.stdout.close()
                log.close()
        reader = threading.Thread(target=collect, daemon=True)
        reader.start()
        self.threads.append(reader)
        deadline = time.monotonic() + self.args.startup_timeout
        while not ready.wait(0.1):
            if proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f'{name} did not become ready; inspect {name}.log')
        # The protocol has no positive login acknowledgement. Catch immediate rejection.
        time.sleep(self.args.delay)
        if proc.poll() is not None:
            raise RuntimeError(f'{name} disconnected after login; inspect {name}.log')
        print(f'{name}: connection opened and login sent (not an authentication acknowledgement).')
        greeting = GREETINGS[self.names.index(name)].format(name=name)
        self.send(name, 'world ' + greeting)

    def observe_group(self, name, line):
        prefix = 'Received:GroupInfo:GroupAdmin::'
        if line.startswith(prefix):
            fields = line[len(prefix):].split(';')
            if len(fields) < 5:
                return
            group_id, leader = fields[:2]
            try:
                count, error = int(fields[3]), int(fields[4])
            except ValueError:
                return
            members = [entry.rsplit(',', 1) for entry in fields[5:]]
            if (error != 0 or not group_id or len(members) != count or
                    any(len(entry) != 2 or entry[1] not in ('0', '1') for entry in members)):
                return
            names = {entry[0] for entry in members}
            with self.group_lock:
                for bot in self.names:
                    if bot in names:
                        self.groups[bot] = group_id
                    elif self.groups[bot] == group_id:
                        self.groups[bot] = ''
        elif line.startswith('CONTROL SENT leave'):
            # A sent command is not a server acknowledgement.
            with self.group_lock:
                self.groups[name] = None
        elif line.startswith('Received:GroupDisbandInfo:'):
            fields = line.split(':', 4)
            if len(fields) == 5 and fields[4] == 'left the group':
                departed = fields[2]
                with self.group_lock:
                    if (departed in self.groups and self.groups[name] and
                            self.groups[departed] == self.groups[name]):
                        self.groups[departed] = ''

    def show_groups(self):
        with self.group_lock:
            snapshot = dict(self.groups)
        print('Bot          Process   Last reported group ID')
        for name in self.names:
            proc = self.processes.get(name)
            state = 'running' if proc and proc.poll() is None else 'stopped'
            group = snapshot[name]
            display = 'unknown (no confirmed update)' if group is None else (group or 'none')
            print(f'{name:<12} {state:<9} {display}')
        print('Cached server updates; stopped bots may still belong to their last group.')

    def send(self, name, command):
        if name not in self.names:
            raise ValueError(f'Unknown bot: {name}')
        proc = self.processes.get(name)
        if proc is None or proc.poll() is not None:
            raise ValueError(f'{name} is stopped; use reconnect {name}')
        proc.stdin.write(command + '\n')
        proc.stdin.flush()
        print(f'{name} -> {command}', flush=True)
        time.sleep(self.args.delay)

    def stop(self, name):
        proc = self.processes.get(name)
        if proc is None:
            return
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        proc.stdin.close()

    def close(self):
        for name in list(self.processes):
            self.stop(name)
        for thread in self.threads:
            thread.join(timeout=3)

    def reset(self):
        for name in self.names:
            proc = self.processes.get(name)
            if proc and proc.poll() is None:
                self.send(name, 'leave')

    def scenario(self, kind):
        if kind not in ('bots', 'unreal', 'unreal-led'):
            raise ValueError('scenario must be bots, unreal, or unreal-led')
        self.reset()
        leader = self.names[0]
        if kind == 'unreal-led':
            print(f'In Unreal, leave any old group, then invite {", ".join(self.names[:5])}. '
                  'Bots accept automatically. The sixth bot stays outside.')
            return
        if kind == 'unreal':
            print(f'Ensure {self.args.unreal_character} is connected and outside any old group.')
            input('Press Enter to send the Unreal invitation... ')
            self.send(leader, 'invite ' + self.args.unreal_character)
            input('Accept the invitation in Unreal, then press Enter to invite four bots... ')
            targets = self.names[1:5]
        else:
            targets = self.names[1:]
        for target in targets:
            self.send(leader, 'invite ' + target)
        print('Scenario commands sent. Inspect group roster updates; this is not an assertion of success.')

    def execute(self, line):
        words = shlex.split(line)
        if not words:
            return True
        action, *args = words
        if action in ('quit', 'exit'):
            return False
        if action == 'help':
            print(HELP)
        elif action == 'status':
            for name in self.names:
                p = self.processes.get(name)
                print(f'{name}: {"running" if p and p.poll() is None else "stopped"}')
        elif action == 'groups' and not args:
            self.show_groups()
        elif action == 'scenario' and len(args) == 1:
            self.scenario(args[0])
        elif action == 'reset' and not args:
            self.reset()
        elif action in ('invite', 'kick') and len(args) == 2:
            self.send(args[0], action + ' ' + args[1])
        elif action == 'leave' and len(args) == 1:
            self.send(args[0], 'leave')
        elif action in ('disconnect', 'reconnect') and len(args) == 1:
            if args[0] not in self.names:
                raise ValueError('Unknown bot')
            self.stop(args[0])
            if action == 'reconnect':
                self.start(args[0])
        else:
            raise ValueError('Unknown command or wrong arguments; type help')
        return True


HELP = '''Commands:
  scenario bots        six bots in one group
  scenario unreal      Unreal + five bots; prompts for manual acceptance
  scenario unreal-led  five bots await invitations from Unreal
  invite BOT TARGET    invite another bot or the Unreal character
  kick BOT TARGET      request removal as BOT
  leave BOT            remove BOT from its current group
  disconnect BOT       stop a bot to test offline presence
  reconnect BOT        restart a bot with a fresh JWT
  reset                all running bots leave their groups
  groups               show last reported group ID for every bot
  status               show process status (not authenticated/group status)
  quit                 stop all bots and exit
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unreal-character', required=True)
    parser.add_argument('--prefix', default='TestBot')
    parser.add_argument('--days', type=int, default=1)
    parser.add_argument('--delay', type=float, default=1.0)
    parser.add_argument('--startup-timeout', type=float, default=15)
    parser.add_argument('--bot', type=Path, default=ROOT / 'uDaraChatBot')
    parser.add_argument('--token-generator', type=Path, default=ROOT.parent / 'TokenGenerator/token_generator.py')
    parser.add_argument('--secret-file', type=Path,
                        default=Path(os.environ.get('DARA_CHAT_JWT_SECRET_FILE', ROOT.parent / 'secrets/DARA_CHAT_JWT_SECRET')))
    parser.add_argument('--log-dir', type=Path, default=ROOT / 'log' / time.strftime('group-test-%Y%m%d-%H%M%S'))
    args = parser.parse_args()
    if (args.days <= 0 or not 0 <= args.delay < 300 or not 0 < args.startup_timeout < 600 or
            not args.prefix or any(not (c.isalnum() or c == '_') for c in args.prefix) or
            not args.unreal_character or any(c.isspace() or c in ':;\0' for c in args.unreal_character)):
        parser.error('Invalid name, prefix, validity, delay or timeout')
    if args.unreal_character in [f'{args.prefix}{i:02d}' for i in range(1, 7)]:
        parser.error('Unreal character must differ from bot names')
    controller = None
    try:
        secret = os.environ.get('DARA_CHAT_JWT_SECRET')
        if secret is None:
            secret = args.secret_file.read_text(encoding='utf-8').rstrip('\n')
        if not secret or '\n' in secret or '\r' in secret:
            raise ValueError('Signing secret must be one nonempty line')
        generate = load_generator(args.token_generator.resolve())
        args.bot = args.bot.resolve()
        controller = Controller(args, secret, generate)
        for name in controller.names:
            controller.start(name)
        print(f'Logs: {args.log_dir}\n{HELP}')
        while controller.execute(input('group-test> ')):
            pass
    except (EOFError, KeyboardInterrupt):
        print('\nStopping bots.')
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Controller error: {error}', file=sys.stderr)
        return 1
    finally:
        if controller:
            controller.close()
    return 0


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    sys.exit(main())
