#!/usr/bin/env python3
"""Generates the shipped maps (symmetric layouts are mirrored so both teams get the same map).
The maps are a build artifact: CMake runs this (python3, nothing else needed) and writes them to maps/, which
.gitignore leaves out. Edit the generator and rebuild; never hand-edit the .qmap files it writes.
  tools/maps/prefabs.py     the M builder and the prefabs / helpers more than one map uses
  tools/maps/<name>.py      one module per map: build() makes it and saves it as <name>.qmap
  python3 tools/make_maps.py [--out DIR]   writes every map (default: maps/ next to tools/)
  python3 tools/make_maps.py --map NAME    writes only that map (repeatable)
  python3 tools/make_maps.py --list        prints the names of the maps it makes, writes nothing
Output is byte-for-byte deterministic (seeded generators only; LF line endings on every platform).
The owner's map overrides win (docs/ASSETS.md): a map with a file at assets/overrides/maps/<name>.qmap is written as
that file (line endings normalised), never generated over it."""
import os, sys, argparse, importlib
sys.dont_write_bytecode = True   # (no __pycache__ in the source tree from a build step)
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
# every map, in the order they're made (each is tools/maps/<name>.py, saved as <name>.qmap)
MAPS = ["clockwork", "depot", "summit", "railyard", "canal", "harbor", "bastion", "foundry", "temple", "colosseum",
        "furnace", "dojo", "gantry", "anvil", "viaduct", "hamlet", "downtown", "orbital", "lodestone", "midtown", "hightown",
        "cascade", "castle", "qk1", "qk2", "qk3", "qk4", "datacenter", "mine", "qk5", "qk8", "qk6", "qk7", "sewer", "war", "solar", "rooftoprun", "jungle", "monorail", "dam", "express", "bridge", "convoy", "greenhouse", "orbit", "cruiseship", "fairground", "rooftops", "lighthouse", "terraces", "glacier", "spire"]


def main():
    ap = argparse.ArgumentParser(description="generate the shipped maps")
    ap.add_argument("--out", default=os.path.join(TOOLS, "..", "maps"))
    ap.add_argument("--map", action="append", choices=MAPS, help="make only this map (repeatable)")
    ap.add_argument("--list", action="store_true", help="print the map names instead of writing the maps")
    args = ap.parse_args()
    if args.list:
        print("\n".join(MAPS))
        return
    from maps import prefabs
    prefabs.OUT = args.out
    for name in args.map or MAPS:
        importlib.import_module("maps." + name).build()
        assert prefabs.MADE and prefabs.MADE[-1] == name, "tools/maps/%s.py must save the map as %r" % (name, name)
    print("%d maps written to %s" % (len(prefabs.MADE), os.path.normpath(args.out)))


if __name__ == "__main__":
    main()
