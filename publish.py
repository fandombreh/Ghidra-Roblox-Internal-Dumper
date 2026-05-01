import os
import subprocess
import re
from datetime import datetime

def get_roblox_version():
    offset_file = "Dumps/Offsets.hpp"
    if not os.path.exists(offset_file):
        return None
    
    with open(offset_file, "r") as f:
        content = f.read(1000) # Read header
        match = re.search(r"version-([a-f0-9]+)", content)
        if match:
            return match.group(0)
    return None

def run_command(cmd):
    print(f"Executing: {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return False
    print(result.stdout)
    return True

def main():
    print("🚀 Preparing to publish latest offsets...")
    
    version = get_roblox_version()
    if not version:
        version = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"⚠️ Could not detect version string, using timestamp: {version}")
    else:
        print(f"✅ Detected Roblox version: {version}")

    # Stage changes
    if not run_command("git add Dumps/Offsets.hpp"):
        return

    # Check if there are changes
    status = subprocess.run("git status --porcelain", shell=True, capture_output=True, text=True).stdout
    if "Dumps/Offsets.hpp" not in status:
        print("ℹ️ No changes detected in Offsets.hpp. Nothing to publish.")
        return

    # Commit
    commit_msg = f"Update offsets for {version}"
    if not run_command(f'git commit -m "{commit_msg}"'):
        return

    # Push
    print("📤 Pushing to GitHub... (This will trigger the Auto-Release Action)")
    if run_command("git push origin main"):
        print("\n✨ Success! GitHub will now create a new release automatically.")
        print("🔗 Check your repo releases: https://github.com/fandombreh/Ghidra-Roblox-Internal-Dumper/releases")
    else:
        print("\n❌ Push failed. Make sure you have internet access and git is configured.")

if __name__ == "__main__":
    main()
