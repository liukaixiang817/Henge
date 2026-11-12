#!/usr/bin/env python3
"""Build independently loadable engines. The off path comes from a pinned,
pristine mainline export, never from the Rosetta working tree with a flag off.
No network access or git worktree changes are made by this build step.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shlex
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def output(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', choices=('iphoneos', 'iphonesimulator'), default='iphoneos')
    args = parser.parse_args()
    simulator = args.sdk == 'iphonesimulator'
    revision = (ROOT / 'ios/engine-mainline.txt').read_text().strip()
    resolved = output('git', '-C', ROOT, 'rev-parse', f'{revision}^{{commit}}')
    if resolved != revision:
        raise SystemExit('engine-mainline.txt must pin a full mainline commit ID')
    dest = ROOT / 'build/engines' / args.sdk
    dest.mkdir(parents=True, exist_ok=True)
    pristine = dest / 'mainline' / revision
    paths = ['Makefile', 'core', 'host', 'devices', 'ios/Henge/Native', 'tests']
    # Regenerate and verify every mainline source byte on every build. A stale
    # export or an accidental edit must not silently become the mainline engine.
    archive = output('git', '-C', ROOT, 'rev-parse', f'{revision}^{{tree}}')
    with tempfile.TemporaryFile() as stream:
        run('git', '-C', ROOT, 'archive', revision, *paths, stdout=stream)
        stream.seek(0)
        if pristine.exists():
            shutil.rmtree(pristine)
        pristine.mkdir(parents=True)
        with tarfile.open(fileobj=stream) as tar:
            members = tar.getmembers()
            for member in members:
                if (Path(member.name).is_absolute() or '..' in Path(member.name).parts
                        or not (member.isfile() or member.isdir())):
                    raise SystemExit('Unexpected entry in the pinned source archive')
            options = {'filter': 'data'} if hasattr(tarfile, 'data_filter') else {}
            tar.extractall(pristine, members=members, **options)
    source_hashes = {str(p.relative_to(pristine)): sha(p)
                     for p in sorted(pristine.rglob('*')) if p.is_file()}
    sdk_path = output('xcrun', '--sdk', args.sdk, '--show-sdk-path')
    cc = ['xcrun', '--sdk', args.sdk, 'clang', '-isysroot', sdk_path]
    cc += ['-target', 'arm64-apple-ios17.0-simulator' if simulator else 'arm64-apple-ios17.0']
    deps = Path(os.environ.get('SIMULATOR_DEPS', str(ROOT / 'build/simulator-deps/sysroot'))) if simulator else ROOT / 'ios/deps'
    restore = ROOT / ('build/simulator-restore' if simulator else 'build/ios') / 'libhengerestore.a'
    run(ROOT / 'tools/build_ios_restore.sh', *(['simulator'] if simulator else []))
    frameworks = dest / 'Frameworks'
    frameworks.mkdir(exist_ok=True)
    native = ROOT / 'ios/Henge/Native'
    adapter = ROOT / 'ios/Engine'
    manifest = {'mainline_revision': revision, 'mainline_tree': archive,
                'mainline_files_sha256': source_hashes, 'sdk': args.sdk,
                'xcode': output('xcodebuild', '-version'), 'engines': {}}
    for rosetta, name, source in [(0, 'HengeMainlineEngine', pristine), (1, 'HengeRosettaEngine', ROOT)]:
        objects = dest / 'objects' / name
        run('make', '-C', source, '-j8', f'IB={objects}', f'IOS_DEPS={deps}',
            f'IOS_CC={shlex.join(cc)}', f'ROSETTA_4K_CACHE={rosetta}', objects / 'libhengevm.a')
        flags = ['-std=c11', '-O2', '-fvisibility=hidden', '-Wall', '-Wextra',
                 f'-DHENGE_ENGINE_ROSETTA={rosetta}', f'-DHENGE_ROSETTA_4K_CACHE={rosetta}',
                 f'-DHENGE_MAINLINE_REVISION="{revision}"']
        flags += [f'-I{p}' for p in [adapter, native, source / 'core', source / 'host', source / 'devices']]
        # Share the exact mainline lifecycle bridge, including shutdown/restart
        # cleanup. Only the Rosetta engine's CPU implementation differs.
        backend = pristine / 'ios/Henge/Native/henge-vm.c'
        bridge_objects = []
        for path in [backend, adapter / 'henge-engine-api.c']:
            obj = objects / (path.stem + '.o')
            run(*cc, *flags, '-c', path, '-o', obj)
            bridge_objects.append(obj)
        bundle = frameworks / f'{name}.framework'
        bundle.mkdir(exist_ok=True)
        binary = bundle / name
        libs = ['-l' + lib for lib in ['slirp', 'glib-2.0', 'intl', 'pcre2-8', 'iconv', 'resolv', 'z', 'compression']]
        frameworks_flags = [item for framework in ['Foundation', 'CoreFoundation', 'Security', 'CFNetwork',
                            'SystemConfiguration', 'Metal', 'IOSurface', 'AudioToolbox', 'AVFAudio', 'QuartzCore', 'CoreGraphics']
                            for item in ['-framework', framework]]
        run(*cc, '-dynamiclib', '-Wl,-dead_strip', '-Wl,-exported_symbol,_henge_engine_get_api',
            '-install_name', f'@rpath/{name}.framework/{name}', *bridge_objects,
            objects / 'libhengevm.a', restore, f'-L{deps / "lib"}', *libs, *frameworks_flags, '-o', binary)
        plist = {'CFBundleExecutable': name, 'CFBundleIdentifier': f'com.henge.{name}',
                 'CFBundleName': name, 'CFBundlePackageType': 'FMWK', 'CFBundleInfoDictionaryVersion': '6.0',
                 'CFBundleShortVersionString': '1.0', 'CFBundleVersion': '1', 'MinimumOSVersion': '17.0',
                 'CFBundleSupportedPlatforms': ['iPhoneSimulator' if simulator else 'iPhoneOS'],
                 'HengeMainlineRevision': revision, 'HengeRosettaEngine': bool(rosetta),
                 'HengeSoftwareTLBPageBytes': 4096 if rosetta else 16384}
        (bundle / 'Info.plist').write_bytes(plistlib.dumps(plist))
        exports = output('xcrun', 'nm', '-gUj', binary).splitlines()
        if exports != ['_henge_engine_get_api']:
            raise SystemExit(f'{name}: engine implementation symbols escaped the framework: {exports}')
        # File-provider metadata in Documents is not valid in a signed bundle.
        with tempfile.TemporaryDirectory(prefix='henge-engine-sign-') as stage:
            clean = Path(stage) / bundle.name
            run('ditto', '--norsrc', '--noextattr', bundle, clean)
            run('codesign', '--force', '--sign', '-', '--timestamp=none', clean)
            shutil.rmtree(bundle)
            run('ditto', '--norsrc', '--noextattr', clean, bundle)
        manifest['engines'][name] = {'binary_sha256': sha(binary), 'rosetta': bool(rosetta),
                                      'backend_sha256': sha(backend), 'exports': exports,
                                      'software_tlb_page_bytes': 4096 if rosetta else 16384}
        print(f'Built {name}: {binary.stat().st_size:,} bytes', flush=True)
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
