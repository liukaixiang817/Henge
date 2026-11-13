"""Verify the actual IPA/TIPA payload produced for the iCloud test branch."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import stat
import zipfile


def validate(path, commit):
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None, "archive integrity failed"
        names = archive.namelist()
        assert all(name.startswith("Payload/") for name in names)
        forbidden = {"machine.json", "identity.json", "lab-task.sh", "lab-result.txt"}
        assert not any(name.endswith((".img", ".ram", ".log")) or
                       name.rsplit("/", 1)[-1] in forbidden or
                       "AVPBooter" in name or "AVPSEPBooter" in name for name in names)
        root = "Payload/Henge.app/"
        info = plistlib.loads(archive.read(root + "Info.plist"))
        helper = plistlib.loads(archive.read(root + "PlugIns/HengeJITHelper.appex/Info.plist"))
        assert info["CFBundleIdentifier"] == "com.henge.icloudsoftware"
        assert info["CFBundleDisplayName"] == "Henge iCloud"
        assert info["CFBundleShortVersionString"] == "0.3.0"
        assert info["CFBundleVersion"] == "104"
        assert helper["CFBundleIdentifier"] == "com.henge.icloudsoftware.JITHelper"
        assert helper["CFBundleVersion"] == "104"
        assert info.get("NSCameraUsageDescription") and info.get("NSMicrophoneUsageDescription")
        if commit:
            assert info["HengeBuildCommit"] == commit[:12], "wrong source commit packaged"
        executable = archive.read(root + "Henge")
        assert (archive.getinfo(root + "Henge").external_attr >> 16) & stat.S_IXUSR
        for marker in [b"macOS 15.6.1 (24G90)", b"macOS 26.6.2 (25G83)",
                       b"macOS 27.2 (26B123)", b"unused Mac app address reservations for the VM"]:
            assert marker in executable, "required compatibility or memory fix missing"
        assert b"_shouldUsePSCBAA" not in executable, "retired diagnostic override included"
    return {"file": path.name, "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "bundleID": info["CFBundleIdentifier"], "build": info["CFBundleVersion"],
            "commit": info.get("HengeBuildCommit"), "archiveEntries": len(names)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {"packages": [validate(path, args.commit) for path in args.packages],
              "supportedExactGuestBuilds": ["24G90", "25G83", "26B123"],
              "requiresNewVM": True, "requiresColdBoot": True,
              "cloudAccountsTestedInCI": False, "physicalIOSDeviceTestedInCI": False,
              "iMessageAndFaceTimeGuaranteed": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
