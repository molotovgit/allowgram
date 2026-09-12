"""Run only synthetic, network-disabled native policy fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from native_process import process_identity, require_session_zero, stop_owned

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
require_session_zero()
executable = args.executable.resolve(strict=True)
build = json.loads((executable.parent / 'build-evidence.json').read_text())
if not build.get('hardeningFixture') or not build.get('mtprotoNetworkDisabled'):
    raise RuntimeError('Only a declared network-disabled hardening fixture may run.')
if hashlib.sha256(executable.read_bytes()).hexdigest() != build['fixtureExecutableSha256']:
    raise RuntimeError('Fixture executable hash differs from its build receipt.')
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
(output / 'roaming').mkdir()
(output / 'local').mkdir()
report = output / 'hardening.json'
env = dict(os.environ, APPDATA=str(output / 'roaming'), LOCALAPPDATA=str(output / 'local'),
           ALLOWGRAM_HARDENING_REPORT=str(report), ALLOWGRAM_UI_SCALE='100')
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen([str(executable), '-many', '-noupdate', '-workdir', str(output)],
                           cwd=output, env=env, startupinfo=startup)
identity = process_identity(process, executable)
(output / 'process.json').write_text(json.dumps(identity, indent=2) + '\n')
try:
    code = process.wait(timeout=60)
except subprocess.TimeoutExpired:
    stop_owned(process, executable, identity)
    (output / 'timeout.json').write_text(json.dumps(identity, indent=2))
    raise RuntimeError("Native fixture timed out; owned process stopped.") from None
results = []
for path in (report, Path(str(report) + '.composer.json')):
    if not path.is_file():
        results.append(dict(checks=[], failures=1, finished=False, missing=path.name))
        print(f'Missing native receipt: {path.name}; process exit {code}', flush=True)
        continue
    data = json.loads(path.read_text())
    results.append(data)
    print(f'{path.name}: {len(data["checks"])} checks, {data["failures"]} failures, exit {code}', flush=True)
    for check in data['checks']:
        if not check['pass']:
            print('FAIL: ' + check['name'], flush=True)
receipt = dict(process=identity, processExitCode=code, results=results,
               windowsSession=0, synthetic=True, mtprotoNetworkDisabled=True)
(output / 'results.json').write_text(json.dumps(receipt, indent=2) + '\n')
raise SystemExit(1 if code or any(data['failures'] or not data.get('finished') for data in results) else 0)
