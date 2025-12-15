Import("env")

import os
import subprocess
from datetime import datetime, timezone


def _run_git(args):
    try:
        out = subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL)
        return out.decode("utf-8", errors="replace").strip()
    except Exception:
        return None


def _is_git_dirty():
    try:
        subprocess.check_call(["git", "diff", "--quiet"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.check_call(["git", "diff", "--cached", "--quiet"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return False
    except Exception:
        return True


sha = os.environ.get("GITHUB_SHA")
sha_short = None

if not sha:
    sha = _run_git(["rev-parse", "HEAD"])

if sha:
    sha_short = sha[:7]

if not sha_short:
    sha_short = _run_git(["rev-parse", "--short", "HEAD"]) or "unknown"

if not sha:
    sha = sha_short

build_date = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
dirty = "0"
try:
    if _run_git(["rev-parse", "--is-inside-work-tree"]) == "true":
        dirty = "1" if _is_git_dirty() else "0"
except Exception:
    dirty = "0"
env.Append(
    CPPDEFINES=[
        ("WT_FW_GIT_SHA", '\\"%s\\"' % sha),
        ("WT_FW_GIT_SHA_SHORT", '\\"%s\\"' % sha_short),
        ("WT_FW_BUILD_DATE", '\\"%s\\"' % build_date),
        ("WT_FW_GIT_DIRTY", dirty),
    ]
)
