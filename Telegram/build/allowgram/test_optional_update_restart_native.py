"""Run isolated native optional-updater restart fixtures."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--package', type=Path)
args = parser.parse_args()

executable = args.executable.resolve()
output = args.output.resolve()
package = args.package.resolve() if args.package else None
if output.exists():
    raise RuntimeError('Optional updater restart output directory must be new.')
output.mkdir(parents=True)
build = json.loads((executable.parent / 'build-evidence.json').read_text(encoding='utf-8'))
if not build.get('optionalRestartFixture'):
    raise RuntimeError('Only a declared optional-updater restart fixture may run.')
if hashlib.sha256(executable.read_bytes()).hexdigest() != build['fixtureExecutableSha256']:
    raise RuntimeError('Fixture executable hash differs from build evidence.')
if package is not None and not package.is_file():
    raise RuntimeError(f'Missing signed update fixture package: {package}')

all_checks = []


def check(pass_value: bool, name: str, detail: str = '') -> None:
    all_checks.append({'name': name, 'pass': bool(pass_value), 'detail': detail})


def argument_tokens(arguments: str) -> list[str]:
    return shlex.split(arguments)


def run_client(name: str, profile: Path, env_updates: dict[str, str], extra_args: list[str]) -> tuple[subprocess.CompletedProcess[bytes], Path]:
    profile.mkdir()
    report_dir = output / name
    report_dir.mkdir()
    env = os.environ.copy()
    env.update(
        ALLOWGRAM_UI_SCALE='100',
        QT_SCALE_FACTOR='1',
        **env_updates,
    )
    process = subprocess.Popen(
        [str(executable), '-many', '-workdir', str(profile), *extra_args],
        cwd=executable.parent,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        stdout, stderr = process.communicate(timeout=180)
    except subprocess.TimeoutExpired as exc:
        process.kill()
        process.communicate()
        raise RuntimeError(f'{name} fixture timed out; owned process stopped.') from exc
    completed = subprocess.CompletedProcess(
        process.args,
        process.returncode,
        stdout=stdout,
        stderr=stderr,
    )
    (report_dir / 'stdout.txt').write_bytes(stdout)
    (report_dir / 'stderr.txt').write_bytes(stderr)
    if process.returncode != 0:
        raise RuntimeError(
            f'{name} fixture exited {process.returncode}: '
            + stderr.decode(errors='replace')[-4000:]
        )
    return completed, report_dir


def read_json(path: Path, description: str) -> dict:
    if not path.is_file():
        raise RuntimeError(f'Missing {description}: {path}')
    return json.loads(path.read_text(encoding='utf-8'))


def check_direct() -> dict:
    profile = output / 'direct-profile'
    launcher_report = output / 'direct-restart-report.json'
    completed, report_dir = run_client(
        'direct',
        profile,
        {'ALLOWGRAM_OPTIONAL_RESTART_REPORT': str(launcher_report)},
        ['-noupdate'],
    )
    observed = read_json(launcher_report, 'direct restart launcher receipt')
    arguments = observed.get('arguments', '')
    tokens = argument_tokens(arguments)
    binary_path = observed.get('binaryPath', '')
    check(binary_path.lower().endswith('allowgram-docs.exe'), 'direct restart relaunches the owned client fixture', binary_path)
    check('-noupdate' in tokens, 'ordinary relaunch keeps updater startup disabled', arguments)
    check('-tosettings' in tokens, 'direct restart preserves settings restart intent', arguments)
    check('-update' not in tokens and '-stagehash' not in tokens, 'failed readiness does not launch update helper arguments', arguments)
    check(observed.get('restartingUpdate') is False, 'failed readiness does not arm update restart')
    check(observed.get('restarting') is True, 'failed readiness falls back to ordinary restart')
    check(observed.get('readyStageHash') == '', 'unchecked ready state leaves no authenticated stage hash')
    return {
        'processExitCode': completed.returncode,
        'profile': str(profile),
        'reportDirectory': str(report_dir),
        'launcherReport': str(launcher_report),
        'observed': observed,
    }


def check_cascade() -> dict | None:
    if package is None:
        return None
    profile = output / 'cascade-profile'
    cascade_report = output / 'cascade-report.json'
    launcher_report = output / 'cascade-launch-report.json'
    completed, report_dir = run_client(
        'cascade',
        profile,
        {
            'ALLOWGRAM_OPTIONAL_RESTART_CASCADE_REPORT': str(cascade_report),
            'ALLOWGRAM_OPTIONAL_RESTART_LAUNCH_REPORT': str(launcher_report),
            'ALLOWGRAM_OPTIONAL_RESTART_PACKAGE': str(package),
        },
        [],
    )
    cascade_observed = read_json(cascade_report, 'cascade confirmation receipt')
    cascade_failures = cascade_observed.get('failures')
    cascade_finished = cascade_observed.get('finished') is True
    final_confirmation = cascade_observed.get('readyForFinalConfirmation') is True
    launcher_observed = None
    if launcher_report.is_file():
        launcher_observed = read_json(launcher_report, 'cascade launcher receipt')
    check(cascade_failures == 0, 'cascade fixture reports no native check failures', json.dumps(cascade_observed, sort_keys=True))
    check(cascade_finished, 'cascade fixture completed after final accepted restart')
    check(final_confirmation, 'cascade reached final user-approved restart confirmation')
    cascade_ready_for_handoff = cascade_failures == 0 and cascade_finished and final_confirmation
    if cascade_ready_for_handoff:
        check(launcher_observed is not None, 'accepted cascade records the update helper launch receipt')
        if launcher_observed is not None:
            launcher_args = launcher_observed.get('arguments', '')
            launcher_tokens = argument_tokens(launcher_args)
            binary_path = launcher_observed.get('binaryPath', '')
            ready_hash = launcher_observed.get('readyStageHash', '')
            check(binary_path.lower().endswith('allowgramupdater.exe'), 'accepted cascade launches the owned update helper', binary_path)
            check('-update' in launcher_tokens, 'accepted cascade passes update helper mode', launcher_args)
            check('-stagehash' in launcher_tokens, 'accepted cascade passes authenticated stage hash', launcher_args)
            check('-exename' in launcher_tokens and 'Allowgram-Docs.exe' in launcher_tokens, 'accepted cascade targets the owned fixture executable', launcher_args)
            check('-noupdate' not in launcher_tokens, 'accepted cascade does not preserve direct no-update fallback flag', launcher_args)
            check('-tosettings' not in launcher_tokens, 'accepted cascade does not turn into a settings restart', launcher_args)
            check(launcher_observed.get('restartingUpdate') is True, 'accepted cascade arms update restart')
            check(launcher_observed.get('restarting') is False, 'accepted cascade does not arm ordinary restart')
            check(isinstance(ready_hash, str) and len(ready_hash) >= 32, 'accepted cascade exposes authenticated ready stage hash', ready_hash)
    else:
        check(launcher_observed is None,
              'failed cascade does not count a captured helper launch as accepted handoff',
              json.dumps(launcher_observed, sort_keys=True) if launcher_observed else '')
    return {
        'processExitCode': completed.returncode,
        'profile': str(profile),
        'reportDirectory': str(report_dir),
        'cascadeReport': str(cascade_report),
        'launcherReport': str(launcher_report),
        'cascadeObserved': cascade_observed,
        'launcherObserved': launcher_observed,
    }


direct = check_direct()
cascade = check_cascade()
failures = [item for item in all_checks if not item['pass']]
result = {
    'finished': True,
    'fixtureExecutable': str(executable),
    'fixtureExecutableSha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
    'package': str(package) if package else None,
    'direct': direct,
    'cascade': cascade,
    'checks': all_checks,
    'failures': len(failures),
}
(output / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
if failures:
    raise SystemExit(1)