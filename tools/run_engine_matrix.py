#!/usr/bin/env python3
"""Run both production engines against separately cloned macOS guests.

The stage contains bin/{simulator_engine_lab,test_engine_selection,Frameworks}
and ID/{vm/{disk.img,auxiliary.bin},share/}. Devices JSON lists id, udid, name.
Guest launchd must fetch share/verify_rosetta.sh at startup (test lab only).
Results are execution through the Simulator, never evidence of physical chips.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--devices', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--ecid', required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    devices = json.loads(args.devices.read_text())
    for device in devices:
        assert (args.stage / device['id'] / 'vm/disk.img').is_file()
    summaries = []

    def check(device):
        directory = args.results / device['id']
        directory.mkdir(parents=True, exist_ok=True)
        with (directory / 'simulator-boot.log').open('w') as log:
            subprocess.run(['xcrun', 'simctl', 'bootstatus', device['udid'], '-b'],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        # Record the Simulator's actual configuration independently of the label.
        info = dict(device)
        live = json.loads(subprocess.check_output(['xcrun', 'simctl', 'list', '--json', 'devices', 'available']))
        for runtime, entries in live['devices'].items():
            for entry in entries:
                if entry['udid'] == device['udid']:
                    info['simulator_device_type'] = entry['deviceTypeIdentifier']
                    info['simulator_runtime'] = runtime
                    info['simulator_name'] = entry['name']
        assert 'simulator_device_type' in info
        info['host_cpu'] = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
        (directory / 'device.json').write_text(json.dumps(info, indent=2) + '\n')
        lab = args.stage / device['id']
        result = lab / 'share/rosetta-result.txt'
        modes = []
        for mode in (0, 1):
            out = directory / ('on' if mode else 'off')
            out.mkdir(exist_ok=True)
            with (out / 'selection.log').open('w') as log:
                subprocess.run(['xcrun', 'simctl', 'spawn', device['udid'],
                                str(args.stage / 'bin/test_engine_selection'), str(mode),
                                str(args.stage / 'bin/Frameworks'), args.revision, 'normal'],
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            for path in [result, lab / 'share/rosetta-progress.txt', lab / 'vm/engine-status.json']:
                path.unlink(missing_ok=True)
            (lab / 'share/engine-mode.txt').write_text(str(mode) + '\n')
            (lab / 'vm/input.txt').write_text('')
            command = ['xcrun', 'simctl', 'spawn', device['udid'], str(args.stage / 'bin/simulator_engine_lab'),
                       '--frameworks', str(args.stage / 'bin/Frameworks'), '--rosetta', str(mode),
                       '--output', str(lab / 'vm'), '--share', str(lab / 'share'), '--rom', str(args.rom),
                       '--disk', str(lab / 'vm/disk.img'), '--aux', str(lab / 'vm/auxiliary.bin'), '--ecid', args.ecid]
            print(f'START {device["name"]}: Rosetta {mode}', flush=True)
            started = time.monotonic()
            success = False
            with (out / 'guest.log').open('w') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                           env={**os.environ, 'SIMCTL_CHILD_HV_PVG_NO_METAL': '1'})
                try:
                    while not result.exists():
                        if process.poll() is not None:
                            raise RuntimeError(f'{device["name"]} mode {mode}: guest exited {process.returncode}')
                        if time.monotonic() - started > 900:
                            raise TimeoutError(f'{device["name"]} mode {mode}: guest result timed out')
                        time.sleep(2)
                    text = result.read_text()
                    # Uploads can be observed while still being written.
                    for _ in range(10):
                        if ('EXTENDED_EXIT=' in text if mode else 'MAINLINE_INTEL_UNSUPPORTED=1' in text):
                            break
                        time.sleep(1)
                        text = result.read_text()
                    (out / 'guest.txt').write_text(text)
                    status = json.loads((lab / 'vm/engine-status.json').read_text())
                    (out / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
                    assert status['engine_rosetta'] == mode and status['revision'] == args.revision
                    assert len(status['loaded_engines']) == 1
                    expected = 'HengeRosettaEngine' if mode else 'HengeMainlineEngine'
                    assert expected + '.framework/' in status['loaded_engines'][0]
                    assert 'ENGINE_MODE=' + str(mode) in text and 'HENGE_ARM_OK' in text and 'ARM_EXIT=0' in text
                    if mode:
                        assert 'HENGE_ROSETTA_OK arch=x86_64 translated=1' in text and 'PROBE_EXIT=0' in text
                        assert 'HENGE_ROSETTA_EXTENDED_OK flags=0 sse=0 mp=0' in text and 'EXTENDED_EXIT=0' in text
                    else:
                        assert 'MAINLINE_INTEL_UNSUPPORTED=1' in text and 'PROBE_EXIT=0' not in text
                    success = True
                finally:
                    (lab / 'vm/input.txt').write_text('stop\n')
                    try:
                        code = process.wait(timeout=45)
                    except subprocess.TimeoutExpired:
                        process.terminate()
                        code = process.wait(timeout=15)
                    if code and success:
                        raise RuntimeError(f'{device["name"]}: stop failed ({code})')
            modes.append({'rosetta': mode, 'passed': success, 'elapsed_seconds': round(time.monotonic() - started, 1)})
            print(f'PASS {device["name"]}: Rosetta {mode}', flush=True)
        return {**info, 'modes': modes}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(check, device): device for device in devices}
        for future in as_completed(futures):
            device = futures[future]
            try:
                summaries.append(future.result())
            except Exception as error:
                summaries.append({**device, 'failed': str(error)})
                print(f'FAIL {device["name"]}: {error}', flush=True)
            (args.results / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    if any('failed' in summary for summary in summaries):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
