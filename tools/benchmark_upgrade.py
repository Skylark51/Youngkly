"""Local-mirror benchmark. Does not contact SSH servers or alter existing report outputs."""
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
folder = root / 'outputs' / 'upgrade-validation'
config = folder / 'fixture-config.json'
is_remote = '--remote' in sys.argv
if is_remote:
    settings = json.loads((root / 'config/youngkly.json').read_text(encoding='utf-8'))
    for key in ['reference_docx', 'phrases_file', 'future_work_file']:
        settings[key] = str(root / settings[key])
    config = folder / 'remote-benchmark-config.json'
    config.write_text(json.dumps(settings, ensure_ascii=False, indent=2), encoding='utf-8')
env = dict(os.environ, PATH='D:/yc-tools/msys64/ucrt64/bin;' + os.environ['PATH'])
results = []
batch = folder / ('benchmark-' + str(time.time_ns()))
batch.mkdir()
for version, binary in [('before', root / 'build/Youngkly.exe'), ('after', root / 'build-upgrade/Youngkly.exe')]:
    for repetition in range(3):
        work = batch / f'{version}-{repetition}'
        work.mkdir(exist_ok=False)
        for temperature in ['cold', 'warm']:
            command = [str(binary), '--config', str(config), '--xlsx', str(root / '.youngkly/source.xlsx'),
                       '--date', '2026-10-21', '--analyze', '--out', 'model.json']
            start = time.perf_counter()
            run = subprocess.run(command, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
            elapsed = (time.perf_counter() - start) * 1000
            (work / f'{temperature}.log').write_bytes(run.stdout)
            if run.returncode:
                raise RuntimeError(run.stdout.decode('utf-8', errors='replace'))
            model = json.loads((work / 'model.json').read_text(encoding='utf-8'))
            if any(a['status'] == 'search_error' for a in model.get('search_audit', [])):
                raise RuntimeError('Remote search failed; see ' + str(work / f'{temperature}.log'))
            metrics = model.get('metrics', {})
            results.append(dict(version=version, repetition=repetition, cache=temperature,
                                wall_ms=elapsed, metrics=metrics))
summary = []
for version in ['before', 'after']:
    for temperature in ['cold', 'warm']:
        rows = [r for r in results if r['version'] == version and r['cache'] == temperature]
        summary.append(dict(version=version, cache=temperature, median_ms=statistics.median(r['wall_ms'] for r in rows)))
(folder / ('remote-benchmark.json' if is_remote else 'benchmark.json')).write_text(json.dumps(dict(runs=results, summary=summary), indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
for row in results:
    if row['version'] == 'after' and row['cache'] == 'warm':
        assert row['metrics'].get('downloads', 0) == 0
        assert row['metrics'].get('gaussian_parses', 0) == 0
