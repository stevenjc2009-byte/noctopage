"""install.py - put build/Noctopage.vpk into Vita3K's ux0:app/NOCT00001 by extracting it
(what Vita3K's own installer does for a homebrew VPK), overwriting files in place.

    python tests/vita3k/install.py [path/to/Noctopage.vpk]

Prints the eboot.bin SHA-256 of the VPK and of the installed copy; exit 1 if they differ.
"""
import hashlib
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VITA3K_UX0 = os.path.join(os.environ.get("APPDATA", ""), "Vita3K", "Vita3K", "ux0")
TITLE_ID = "NOCT00001"


def main() -> int:
    vpk = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Noctopage.vpk")
    dest = os.path.join(VITA3K_UX0, "app", TITLE_ID)
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(vpk) as z:
        want = hashlib.sha256(z.read("eboot.bin")).hexdigest()
        for info in z.infolist():
            target = os.path.normpath(os.path.join(dest, info.filename))
            if not target.startswith(dest):
                print(f"refusing path outside the app folder: {info.filename}")
                return 1
            if info.is_dir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as f:
                f.write(z.read(info))
        names = len(z.infolist())
    with open(os.path.join(dest, "eboot.bin"), "rb") as f:
        got = hashlib.sha256(f.read()).hexdigest()
    print(f"installed {names} entries from {vpk} into {dest}")
    print(f"vpk eboot sha256       {want}")
    print(f"installed eboot sha256 {got}")
    return 0 if want == got else 1


if __name__ == "__main__":
    sys.exit(main())
