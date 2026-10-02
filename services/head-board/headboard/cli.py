import argparse
import os
from pathlib import Path

from .store import Store


def main():
    parser = argparse.ArgumentParser(
        description="Private Allowgram Head administration"
    )
    parser.add_argument(
        "--data",
        default=os.environ.get(
            "HEAD_DATA_DIR", str(Path.home() / ".local/share/allowgram-head")
        ),
    )
    parser.add_argument("--owner", default="8683512953")
    sub = parser.add_subparsers(dest="command", required=True)
    access = sub.add_parser("owner-login")
    access.add_argument("--output", required=True)
    args = parser.parse_args()
    store = Store(args.data, args.owner)
    store.issue_owner_code(args.output)
    print("Single-use owner access code saved privately; expires in 15 minutes.")


if __name__ == "__main__":
    main()
