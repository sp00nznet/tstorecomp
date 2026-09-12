"""Serve a DLC index with some packs taken out of it.

Moving installed packs out of work/assets/dlc does not hold: the client's
DLCTask compares what is on disk against the index the server hands it and
downloads back anything missing, so a pack held back on disk is a pack that
reinstalls itself on the next run. The index is the only place a pack can be
removed from and stay removed.

    python dlc_index_filter.py --exclude XMAS --exclude Christmas
    python dlc_index_filter.py --restore

The index is a zip of one XML file listing about five thousand <Package>
elements, each naming its pack group in <FileName val="GROUP:file.zip" />.
Whole elements are dropped; the per-package CRCs and signatures inside the
ones that remain are untouched, which is why this works at all.

The original index is copied to <name>.orig once and never written again, so
--restore always has something true to go back to.
"""

import argparse
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DLC = os.path.join(HERE, "server", "dlc")

# The index the client asks for is chosen by its version, which run-tsto.bat
# sets to 4.69.0. Named rather than discovered: a wrong guess here edits an
# index nothing reads, and the run then looks like the filter did nothing.
INDEX = "DLCIndex-v4_69_UNTROUBLEDVASES_PATCH2-r497432-LYOXW1QI.zip"

PACKAGE = re.compile(r"[ \t]*<Package\b.*?</Package>\s*", re.S)
FILENAME = re.compile(r'<FileName val="([^"]+)"')


def group_of(package):
    m = FILENAME.search(package)
    if not m:
        return None
    val = m.group(1)
    return val.split(":")[0] if ":" in val else val


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--exclude", action="append", default=[],
                    help="case-insensitive substring of a pack group to drop")
    ap.add_argument("--retier", action="append", default=[],
                    help="FROM=TO: serve the TO-tier file under the name FROM, "
                         "for a client that asks for the wrong tier")
    ap.add_argument("--retier-only", action="append", default=[],
                    help="limit --retier to groups matching this substring")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv[1:])

    path = os.path.join(DLC, INDEX)
    orig = path + ".orig"
    if not os.path.exists(orig):
        shutil.copy2(path, orig)

    with zipfile.ZipFile(orig) as z:
        inner = z.infolist()[0].filename
        xml = z.read(inner).decode("utf8")

    if args.restore:
        shutil.copy2(orig, path)
        print("restored %s from .orig" % INDEX)
        return 0

    groups = {}
    for m in PACKAGE.finditer(xml):
        g = group_of(m.group())
        groups[g] = groups.get(g, 0) + 1

    if args.list:
        for g in sorted(groups):
            print("%-52s %d" % (g, groups[g]))
        return 0

    if not args.exclude and not args.retier:
        ap.error("nothing to do: pass --exclude, --retier, --list or --restore")

    # Relabelling rather than excluding, because the client asks for one tier
    # by name and takes what it is given. Our port reads an uninitialised
    # "forced asset tier" as 0 where a device has -1, so it asks for the
    # lowest tier; handing it the higher-tier file under the name it asked for
    # is how you find out whether the tier is what matters, without touching
    # the runtime at all.
    #
    # The per-package CRC and signature belong to the file, not to the label,
    # so the survivors stay valid and the client's own integrity check passes.
    if args.retier:
        swaps = dict(r.split("=", 1) for r in args.retier)
        only = [o.lower() for o in args.retier_only]
        dropped_t = {}
        retiered = {}

        def relabel(m):
            block = m.group()
            g = group_of(block) or ""
            if only and not any(o in g.lower() for o in only):
                return block
            tm = re.search(r'tier="([^"]*)"', block)
            if not tm:
                return block
            tier = tm.group(1)
            if tier in swaps:
                # The low-tier file the client would have taken.
                dropped_t[tier] = dropped_t.get(tier, 0) + 1
                return ""
            for want, have in swaps.items():
                if tier == have:
                    retiered[have] = retiered.get(have, 0) + 1
                    return block.replace('tier="%s"' % have, 'tier="%s"' % want, 1)
            return block

        xml = PACKAGE.sub(relabel, xml)
        for t, n in sorted(dropped_t.items()):
            print("  dropped tier %-8s %d files" % (t, n))
        for t, n in sorted(retiered.items()):
            print("  served tier %-8s under the name the client asks for: %d files" % (t, n))

    pats = [p.lower() for p in args.exclude]
    dropped = {}

    def keep(m):
        g = group_of(m.group()) or ""
        if any(p in g.lower() for p in pats):
            dropped[g] = dropped.get(g, 0) + 1
            return ""
        return m.group()

    out = PACKAGE.sub(keep, xml)

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(inner, out)

    print("%s: %d packages -> %d" % (INDEX, sum(groups.values()),
                                     sum(groups.values()) - sum(dropped.values())))
    for g in sorted(dropped):
        print("  dropped %-50s %d files" % (g, dropped[g]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
