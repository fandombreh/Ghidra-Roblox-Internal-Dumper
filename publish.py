import os
import subprocess
import re
import sys
import shutil
from datetime import datetime

# Force UTF-8 encoding for stdout
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

REPO = "fandombreh/Ghidra-Roblox-Internal-Dumper"
OFFSET_FILES = ["Dumps/Offsets.hpp", "Offsets.hpp"]

# ─────────────────────────────────────────────
#  Helpers
# ─────────────────────────────────────────────

def run(cmd, silent=False):
    """Run a shell command, return (success, stdout, stderr)."""
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if not silent and result.stdout.strip():
        print(result.stdout.strip())
    if not silent and result.stderr.strip():
        print(result.stderr.strip())
    return result.returncode == 0, result.stdout.strip(), result.stderr.strip()

def gh_available():
    ok, _, _ = run("gh --version", silent=True)
    return ok

def gh_authenticated():
    ok, _, _ = run("gh auth status", silent=True)
    return ok

# ─────────────────────────────────────────────
#  Version + Stats detection
# ─────────────────────────────────────────────

def parse_offset_file(path):
    """Parse Offsets.hpp and return (version, offset_count, date_str)."""
    if not os.path.exists(path):
        return None, 0, None

    version = None
    offset_count = 0
    date_str = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # Version string
    m = re.search(r'version-([a-f0-9]+)', content)
    if m:
        version = m.group(0)

    # Offset count from header comment
    m = re.search(r'Offsets found\s+(\d+)', content)
    if m:
        offset_count = int(m.group(1))
    else:
        # Count inline hex values (0x......) as a proxy
        offset_count = len(re.findall(r'0x[0-9A-Fa-f]+', content))

    # Date
    m = re.search(r'Date\s+([\d\-]+ [\d:]+)', content)
    if m:
        date_str = m.group(1)

    return version, offset_count, date_str

def get_best_offset_file():
    for f in OFFSET_FILES:
        if os.path.exists(f):
            return f
    return None

# ─────────────────────────────────────────────
#  Release creation
# ─────────────────────────────────────────────

def build_release_body(version, offset_count, date_str, roblox_ver):
    now = datetime.utcnow().strftime("%Y-%m-%d %H:%M UTC")
    return f"""## Orion Dumper — Automatic Offset Release

| Field | Value |
|-------|-------|
| **Roblox Version** | `{roblox_ver or 'Unknown'}` |
| **Offsets Found** | `{offset_count:,}` |
| **Dump Date** | {date_str or now} |
| **Release Tag** | `{version}` |

### Files
- `Offsets.hpp` — Full C++ header with all discovered memory offsets

### Usage
```cpp
#include "Offsets.hpp"
// All offsets are relative. Use REBASE(offset) to get absolute addresses.
uintptr_t luaV = REBASE(Offsets::luaV_execute);
```

> **Auto-generated** by [Ghidra-Roblox-Internal-Dumper](https://github.com/{REPO})
"""

def create_release_via_gh(tag, version, offset_count, date_str, roblox_ver, asset_path):
    """Use GitHub CLI to create a release and upload the asset directly."""
    print(f"[GH] Creating release {tag} via GitHub CLI...")

    body = build_release_body(version, offset_count, date_str, roblox_ver)
    body_file = "_release_body.md"
    with open(body_file, "w", encoding="utf-8") as f:
        f.write(body)

    # Check if tag already exists and delete it (update release)
    run(f'gh release delete "{tag}" --repo {REPO} --yes', silent=True)

    cmd = (
        f'gh release create "{tag}" '
        f'--repo {REPO} '
        f'--title "Roblox Offsets — {tag}" '
        f'--notes-file "{body_file}" '
        f'"{asset_path}"'
    )
    ok, stdout, stderr = run(cmd)

    try:
        os.remove(body_file)
    except Exception:
        pass

    if ok:
        print(f"\n[SUCCESS] Release created!")
        print(f"[LINK] https://github.com/{REPO}/releases/tag/{tag}")
        return True
    else:
        print(f"[ERROR] gh release create failed: {stderr}")
        return False

def create_release_via_git(tag, offset_file):
    """Fallback: commit + push, which triggers the GitHub Actions workflow."""
    print("[GIT] Falling back to git push (will trigger GitHub Actions release)...")

    # Stage both offset files
    for f in OFFSET_FILES:
        if os.path.exists(f):
            run(f'git add "{f}"', silent=True)

    ok, status, _ = run("git status --porcelain", silent=True)
    if not status:
        print("[-] No changes detected in offset files. Nothing to push.")
        return False

    commit_msg = f"Update offsets for {tag}"
    ok, _, err = run(f'git commit -m "{commit_msg}"')
    if not ok and "nothing to commit" not in err:
        print(f"[ERROR] git commit failed: {err}")
        return False

    print("[GIT] Pushing to GitHub (Actions will create release automatically)...")
    ok, _, err = run("git push origin main")
    if ok:
        print(f"\n[SUCCESS] Pushed! GitHub Actions will create the release shortly.")
        print(f"[LINK] https://github.com/{REPO}/releases")
        return True
    else:
        print(f"[ERROR] git push failed: {err}")
        print("Make sure you have internet access and git credentials configured.")
        return False

# ─────────────────────────────────────────────
#  Main
# ─────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  Orion Dumper — GitHub Release Publisher")
    print("=" * 60)

    offset_file = get_best_offset_file()
    if not offset_file:
        print("[ERROR] No Offsets.hpp found in Dumps/ or root. Run the dumper first.")
        sys.exit(1)

    print(f"[+] Using offset file: {offset_file}")

    version, offset_count, date_str = parse_offset_file(offset_file)

    if version:
        tag = version
        roblox_ver = version
        print(f"[+] Detected Roblox version : {version}")
    else:
        tag = "v" + datetime.utcnow().strftime("%Y.%m.%d-%H%M")
        roblox_ver = "Unknown"
        print(f"[!] No version string found. Using timestamp tag: {tag}")

    print(f"[+] Offsets in file         : {offset_count:,}")
    print(f"[+] Release tag             : {tag}")
    print()

    # Prefer GitHub CLI for immediate release creation
    if gh_available():
        if gh_authenticated():
            success = create_release_via_gh(tag, version, offset_count, date_str, roblox_ver, offset_file)
        else:
            print("[!] GitHub CLI found but not authenticated.")
            print("    Run: gh auth login")
            print("    Falling back to git push method...")
            success = create_release_via_git(tag, offset_file)
    else:
        print("[!] GitHub CLI (gh) not found. Using git push method.")
        print("    Tip: Install gh from https://cli.github.com for instant releases.")
        success = create_release_via_git(tag, offset_file)

    print()
    if success:
        print("[DONE] Release process complete.")
    else:
        print("[FAILED] Release was not created. Check errors above.")
        sys.exit(1)

if __name__ == "__main__":
    main()
