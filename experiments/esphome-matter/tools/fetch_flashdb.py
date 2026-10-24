"""Fetch the exact LibreTiny FlashDB revision used by the storage host tests."""

import subprocess
from pathlib import Path

REVISION = "d5c892f0c545884e9f0de0cf67215e426dc8109d"
REPOSITORY = "https://github.com/libretiny-eu/library-flashdb.git"
DESTINATION = Path(__file__).resolve().parents[1] / ".cache/flashdb"


def main():
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    if not DESTINATION.exists():
        subprocess.run(["git", "init", str(DESTINATION)], check=True)
        subprocess.run(
            ["git", "-C", str(DESTINATION), "remote", "add", "origin", REPOSITORY],
            check=True,
        )
    subprocess.run(
        ["git", "-C", str(DESTINATION), "fetch", "--depth", "1", "origin", REVISION],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(DESTINATION), "checkout", "--detach", REVISION], check=True
    )


if __name__ == "__main__":
    main()
