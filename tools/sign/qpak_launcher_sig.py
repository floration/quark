#!/usr/bin/env python3
"""CI (release.yml, via sign_release.sh --launchers): signs the launcher inside a .qpak and adds the signature to the
package next to it, as quark_launcher[.exe].sig (docs/SIGNING.md). The game and the launcher's self-update only put a
packaged launcher in use when that signature verifies against the release key (owner decision B6).
  qpak_launcher_sig.py QUARK_SIGN PACKAGE...
QUARK_SIGN is the built quark_sign tool (it reads the key from $QUARK_SIGNING_KEY). Run it before the release manifest
is made and the packages are signed: it changes the package."""
import os, struct, subprocess, sys, tempfile

LAUNCHERS = ("quark_launcher", "quark_launcher.exe")


def entries(data):
    """(name, mode, payload) for every file in a qpak (the format tools/pack.py writes)."""
    if not data.startswith(b"QPAK1\n"):
        raise SystemExit("not a qpak")
    at, out = 6, []
    while at < len(data):
        (nl,) = struct.unpack_from("<I", data, at)
        name = data[at + 4:at + 4 + nl].decode()
        mode, size = struct.unpack_from("<IQ", data, at + 4 + nl)
        start = at + 4 + nl + 12
        if start + size > len(data):
            raise SystemExit("truncated qpak")
        out.append((name, mode, data[start:start + size]))
        at = start + size
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    tool = sys.argv[1]
    for pak in sys.argv[2:]:
        data = open(pak, "rb").read()
        files = entries(data)
        names = {n for n, _, _ in files}
        found = [n for n in LAUNCHERS if n in names]
        if len(found) != 1:
            raise SystemExit("%s: expected exactly one packaged launcher (%s), found %s" % (pak, " / ".join(LAUNCHERS), found or "none"))
        name = found[0]
        if name + ".sig" in names:
            raise SystemExit("%s: already has %s.sig" % (pak, name))
        payload = next(d for n, _, d in files if n == name)
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, name)
            with open(exe, "wb") as f:
                f.write(payload)
            subprocess.run([tool, "sign", exe], check=True)
            subprocess.run([tool, "verify", exe], check=True)
            sig = open(exe + ".sig", "rb").read()
        entry = (name + ".sig").encode()
        with open(pak, "ab") as f:
            f.write(struct.pack("<I", len(entry)) + entry + struct.pack("<IQ", 0o644, len(sig)) + sig)
        print("%s: added %s.sig" % (pak, name))


if __name__ == "__main__":
    main()
