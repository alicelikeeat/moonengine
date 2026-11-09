#!/usr/bin/env python3
"""Fail if any commit in BASE..HEAD lacks a DCO 'Signed-off-by' trailer
matching its author.

Usage: check_dco.py <base-sha> <head-sha>
"""
import subprocess
import sys


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    base, head = sys.argv[1], sys.argv[2]
    commits = git("rev-list", "--no-merges", f"{base}..{head}").split()
    missing = []
    for sha in commits:
        author = git("show", "-s", "--format=%an <%ae>", sha).strip()
        body = git("show", "-s", "--format=%B", sha)
        signoffs = [line.split(":", 1)[1].strip() for line in body.splitlines()
                    if line.lower().startswith("signed-off-by:")]
        if author not in signoffs:
            missing.append((sha[:12], author))
    for sha, author in missing:
        print(f"{sha}: missing 'Signed-off-by: {author}'")
    if missing:
        print("\nSign your commits with `git commit -s`. See CONTRIBUTING.md.")
        return 1
    print(f"{len(commits)} commit(s) signed off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
