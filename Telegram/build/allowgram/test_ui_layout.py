"""Run the real-widget overlay in fresh profiles; never use a signed-in app."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
executable = args.executable.resolve(strict=True)
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
results = []
for scale in (100, 125, 150, 200):
    for label, width, height in (('minimum', 380, 480), ('compact', 800, 598), ('desktop', 1100, 800)):
        profile = output / f'{scale}-{label}'
        profile.mkdir()
        (profile / 'roaming').mkdir()
        report = profile / 'geometry.json'
        env = dict(os.environ, APPDATA=str(profile / 'roaming'),
                   ALLOWGRAM_UI_REPORT=str(report), ALLOWGRAM_UI_SCALE=str(scale),
                   ALLOWGRAM_UI_WIDTH=str(round(width * scale / 100)),
                   ALLOWGRAM_UI_HEIGHT=str(round(height * scale / 100)),
                   ALLOWGRAM_DOCS_SCENE='overview', QT_SCALE_FACTOR='1')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen([str(executable), '-many', '-noupdate', '-workdir', str(profile)],
                                   cwd=profile, env=env, startupinfo=startup)
        try:
            code = process.wait(timeout=45)
        except subprocess.TimeoutExpired:
            process.kill()  # Only the owned, freshly launched test process.
            process.wait()
            raise RuntimeError(f'Test timed out: {profile.name}') from None
        if not report.is_file():
            raise RuntimeError(f'No geometry report: {profile.name}, exit {code}')
        data = json.loads(report.read_text())
        data['case'] = profile.name
        data['processExitCode'] = code
        results.append(data)
        print(f'{profile.name}: {len(data["checks"])} checks, {data["failures"]} failures, exit {code}', flush=True)
        for check in data['checks']:
            if not check['pass']:
                print('  FAIL: ' + check['name'], flush=True)
(output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
raise SystemExit(1 if any(r['failures'] or r['processExitCode'] for r in results) else 0)
