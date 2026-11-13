"""Check the pinned compatibility parameters against an Apple vma2 Mach-O.

Usage: python3 tools/audit_icloud_kernel.py build/restore-components/*/kernel.macho
The input is an offline decompressed restore kernel, never process memory.
"""
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


def commands(data, start):
    offset = start + 32
    for _ in range(struct.unpack_from("<I", data, start + 16)[0]):
        command, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError("invalid Mach-O command")
        yield command, offset, size
        offset += size


def audit(path, match, profiles):
    data = path.read_bytes()
    if data[:4] != b"\xcf\xfa\xed\xfe":
        raise ValueError("expected decompressed 64-bit Mach-O")
    segments, kernel = [], None
    for command, offset, size in commands(data, 0):
        if command == 0x19:
            segments.append(struct.unpack_from("<QQQQ", data, offset + 24))
        if command == 0x80000035:
            _, fileoff, nameoff, _ = struct.unpack_from("<QQII", data, offset + 8)
            name = data[offset + nameoff:offset + size].split(b"\0")[0]
            if name == b"com.apple.kernel":
                kernel = fileoff
    if kernel is None:
        raise ValueError("missing kernel fileset entry")

    def virtual(fileoff):
        return next(vm + fileoff - off for vm, _, off, size in segments if off <= fileoff < off + size)

    def physical(address):
        return next(off + address - vm for vm, _, off, size in segments if vm <= address < vm + size)

    symtab = next(struct.unpack_from("<IIII", data, off + 8) for cmd, off, _ in commands(data, kernel) if cmd == 2)
    symboloff, count, stringoff, stringsize = symtab
    proc = None
    for i in range(count):
        nameoff, _, _, _, value = struct.unpack_from("<IBBHQ", data, symboloff + i * 16)
        end = data.index(b"\0", stringoff + nameoff, stringoff + stringsize)
        if data[stringoff + nameoff:end] == b"_proc_name":
            proc = physical(value)
            break
    if proc is None:
        raise ValueError("missing proc_name symbol")
    offsets = {(op >> 10) & 0xfff for op in struct.unpack_from("<32I", data, proc)
               if op & 0xffc003ff == 0x91000001}  # ADD x1,x0,#imm
    if len(offsets) != 1:
        raise ValueError("ambiguous proc command offset")
    comm = offsets.pop()
    accessor = struct.pack("<6I", 0x91000008 | ((comm + 17) << 10),
                           0x39400009 | ((comm + 17) << 10),
                           0x9100000a | (comm << 10),
                           0x7100013f, 0x9a880140, 0xd65f03c0)
    if accessor not in data[proc:proc + 0x200]:
        raise ValueError("adjacent full-name accessor not verified")

    prefix = struct.pack("<7I", 0xd503237f, 0xd100c3ff, 0xa9014ff4,
                         0xa9027bfd, 0x910083fd, 0xaa0303f3, 0xb9000fff)
    candidates, offset = [], 0
    while (offset := data.find(prefix, offset)) >= 0:
        code = data[offset:offset + 104]
        if match(code, len(code)):
            candidates.append(offset)
        offset += 4
    if len(candidates) != 1:
        raise ValueError("handler is not unique")
    handler = virtual(candidates[0])
    code = data[candidates[0]:candidates[0] + 104]
    adrp, add = struct.unpack_from("<II", code, 28)
    immediate = ((adrp >> 5 & 0x7ffff) << 2) | (adrp >> 29 & 3)
    if immediate & (1 << 20):
        immediate -= 1 << 21
    key = ((handler + 28) & ~0xfff) + immediate * 4096 + (add >> 10 & 0xfff)
    if data[physical(key):physical(key) + 12] != b"vmm-present\0":
        raise ValueError("handler key does not match")
    versionoff = data.index(b"Darwin Kernel Version ")
    version = data[versionoff:data.index(b"\0", versionoff)].decode()
    delta = handler - virtual(versionoff)
    if (version, delta, comm) not in profiles:
        raise ValueError("native profile does not match the measured kernel")
    return {"file": str(path), "sha256": hashlib.sha256(data).hexdigest(),
            "version": version, "versionDelta": hex(delta), "procComm": hex(comm),
            "fullNameOffset": hex(comm + 17), "handler": hex(handler),
            "handlerUnique": True, "nativeMatcherAccepted": True,
            "keyVerified": True, "profileVerified": True}


def main():
    library = Path("build/lib-legacy-policy.dylib")
    library.parent.mkdir(exist_ok=True)
    subprocess.run(["clang", "-shared", "-fPIC", "-std=c11", "-Icore", "-Ihost", "-Idevices",
                    "host/legacy_vmm_policy.c", "-o", str(library)], check=True)
    loaded = ctypes.CDLL(str(library.resolve()))
    match = loaded.hv_legacy_vmm_match
    match.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    source = Path("host/legacy_vmm.c").read_text()
    profiles = {(version, int(delta, 16), int(comm, 16)) for version, delta, comm in re.findall(
        r'"(Darwin Kernel Version [^"\n]+)",\s*(0x[0-9a-f]+),(0x[0-9a-f]+)', source)}
    results = [audit(Path(path), match, profiles) for path in sys.argv[1:]]
    if not results:
        raise SystemExit("Provide a decompressed Apple vma2 restore kernel")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
