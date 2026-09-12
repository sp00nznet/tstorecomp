"""Install the DLC packs the game asked for, from the sidecar's own store.

The game does not treat DLC as a download it might skip. On the first run it
fopen()s `<assets>/dlc/<group>/<pack>/0` for every pack its index names, and
whatever is missing it fetches from the CDN -- but its DLCTask completed
without fetching anything, and the asset layer went on to dereference a stream
it never opened. A pack that is not there is not an empty pack to this engine;
it is a pointer field left at -1.

So the packs go in by hand, which is the same content by the same names: the
sidecar serves `dlc/<group>/<pack>.zip` and the client extracts it to
`dlc/<group>/<pack>/`, where the zip's two entries are named "0" and "1". No
transformation, just the step the client would have taken.

Which packs to install comes from the client itself. A run with
ARC_TRACE_FILES=1 logs one line per pack it looked for and did not find, so
the log *is* the list -- more reliable than re-deriving it from the index,
because it is what this build, at this version, on this flavour, actually
wanted.

    python install_dlc.py <run.log> [more.log ...]

Nothing is ever deleted or overwritten: a pack already installed is left
alone, so this is safe to re-run after each attempt.
"""

import os
import re
import sys
import zipfile

STORE = r"G:\recomp\tstorecomp\server\dlc"
INSTALL = r"G:\recomp\tstorecomp\work\assets\dlc"

# [file] fopen    MISS  G:/recomp/.../assets/dlc/<group>/<pack>/0
WANTED = re.compile(r"/assets/dlc/([^/]+)/([^/]+)/[01]\s*$")


def wanted_from(paths):
    """Every (group, pack) a log says the client looked for."""
    out = []
    seen = set()
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "MISS" not in line:
                    continue
                m = WANTED.search(line.rstrip())
                if not m:
                    continue
                key = m.groups()
                if key not in seen:
                    seen.add(key)
                    out.append(key)
    return out


def install(group, pack):
    """Returns "installed", "present", "no zip", or an error string."""
    target = os.path.join(INSTALL, group, pack)
    # "Present" means the entries are there, not just the directory: an
    # interrupted extraction leaves the second file missing, and that is the
    # exact state this exists to fix.
    if os.path.isfile(os.path.join(target, "0")) and os.path.isfile(
        os.path.join(target, "1")
    ):
        return "present"
    src = os.path.join(STORE, group, pack + ".zip")
    if not os.path.isfile(src):
        return "no zip"
    try:
        with zipfile.ZipFile(src) as z:
            os.makedirs(target, exist_ok=True)
            for entry in z.infolist():
                if entry.is_dir():
                    continue
                # The names are "0" and "1" by contract, and a zip that says
                # otherwise is not the pack this expects -- writing it under a
                # made-up name would install something the client cannot read.
                leaf = os.path.basename(entry.filename)
                if leaf not in ("0", "1"):
                    return "unexpected entry " + entry.filename
                with z.open(entry) as fh, open(
                    os.path.join(target, leaf), "wb"
                ) as out:
                    out.write(fh.read())
    except Exception as e:  # a truncated or corrupt zip in the store
        return "failed: %s" % e
    return "installed"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    packs = wanted_from(argv[1:])
    if not packs:
        print("no missing packs in those logs -- was ARC_TRACE_FILES=1 set?")
        return 1
    tally = {}
    problems = []
    for group, pack in packs:
        how = install(group, pack)
        tally[how] = tally.get(how, 0) + 1
        if how not in ("installed", "present"):
            problems.append("%s/%s: %s" % (group, pack, how))
    print("%d packs the client asked for" % len(packs))
    for how in sorted(tally):
        print("  %-9s %d" % (how, tally[how]))
    for line in problems[:20]:
        print("  " + line)
    if len(problems) > 20:
        print("  ...and %d more" % (len(problems) - 20))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
