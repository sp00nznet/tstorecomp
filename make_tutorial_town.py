"""Derive a new-player town from an existing one, so the tutorial runs.

The server's own "blank world" is a LandMessage with nothing in it but
friendData. The client accepts it, goes in-game, and draws black -- because a
town with no innerLandData, no roadsData and no riversData has no terrain, and
a renderer with no terrain has nothing to put on the screen.

What a new player actually gets is a town that is empty of *progress* but not
of ground: the map is there, the roads and rivers are there, and what is
missing is the buildings, the characters, the quests and the level. The flag
that decides whether the tutorial runs is userData.tutorialComplete, which a
played town has set to true.

So this takes a town that works, keeps the parts that describe the world, and
drops the parts that describe the player.

    python make_tutorial_town.py [source] [destination]

Defaults to server/towns/mytown -> server/towns/tutorial. The source is only
ever read.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "server")
sys.path.insert(0, SERVER)
sys.path.insert(0, os.path.join(SERVER, "proto"))

from proto import LandData_pb2  # noqa: E402


# Everything that records what the player has done. Cleared; the world the
# player did it in is kept.
#
# Built by subtraction rather than by construction. A town assembled field by
# field from a blank message renders black: the client needs more of the
# structure than the obvious terrain fields, and working out exactly which is
# an afternoon of runs. Starting from a town that works and taking away what
# is clearly progress needs none of that, and is right by construction about
# everything nobody thought to list.
PROGRESS = (
    # What the player built, who lives there, what they were doing, and the
    # event state that decides which seasonal skin the world wears -- the
    # winter one is what puts snow on every object in the town.
    "buildingData", "characterData", "questData", "jobData",
    "specialEventsData",
)

# Deliberately short. Clearing more than this -- inventory, unlocks,
# spendables, sidebar, object variables -- renders black: something in there
# the client needs is not obviously progress, and finding out exactly which is
# a run per field. These five are enough for a town with nothing built in it.


def build(src):
    out = LandData_pb2.LandMessage()
    out.CopyFrom(src)

    for name in PROGRESS:
        out.ClearField(name)

    # The counts that described what was just removed.
    land = out.innerLandData
    for name in ("numChars", "numBuildings", "numConsumables", "numJobs",
                 "numQuests", "numNotices", "numInventoryItems",
                 "numMemorabiliaItems", "numEventCountLists",
                 "numPremiumUnlocks", "numActionLimits", "numQuestGroups",
                 "numSavedFriends"):
        setattr(land, name, 0)
    land.nextInstanceID = 1
    land.nextCurrencyID = 1
    # "The first save has not happened yet", which is what a new town is.
    land.initialSaveDone = False

    # The player: level one, nothing earned, and the tutorial still to run.
    user = out.userData
    user.level = 1
    user.visualLevel = 1
    user.experience = 0
    user.money = 0
    user.tutorialComplete = False
    user.friendsUnlocked = False
    user.memorabiliaUnlocked = False
    user.reorganizeUnlocked = False
    user.firstPurchase = False
    user.showLevelUp = False
    del user.savedRating[:]

    out.friendData.level = 1
    out.friendData.rating = 0
    out.friendData.name = ""
    out.friendData.boardwalkTileCount = 0
    out.friendListDataIsCreatedAndSaved = False
    return out


def main(argv):
    src_path = argv[1] if len(argv) > 1 else os.path.join(SERVER, "towns", "mytown")
    dst_path = argv[2] if len(argv) > 2 else os.path.join(SERVER, "towns", "tutorial")
    src = LandData_pb2.LandMessage()
    with open(src_path, "rb") as fh:
        src.ParseFromString(fh.read())
    out = build(src)
    blob = out.SerializeToString()
    with open(dst_path, "wb") as fh:
        fh.write(blob)

    owned = out.innerLandData.landBlocks.count("1")
    total = len(out.innerLandData.landBlocks)
    print("%s -> %s (%d bytes)" % (src_path, dst_path, len(blob)))
    print("  kept:    terrain, %dx%d land grid, %d of %d blocks owned"
          % (out.innerLandData.landBlockWidth,
             out.innerLandData.landBlockHeight, owned, total))
    print("  dropped: %d buildings, %d characters, %d quests"
          % (len(src.buildingData), len(src.characterData), len(src.questData)))
    print("  tutorialComplete: %s -> %s"
          % (src.userData.tutorialComplete, out.userData.tutorialComplete))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
