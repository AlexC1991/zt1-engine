"""Looks up the game's text (all its language DLLs, expansions too):
a message's id from words in it, or the text of an id. The id is the number
to search zoo.exe for (Ghidra: Search > For Scalars).

python re/msg.py deeper tank     -> 10560  This object must be placed in a deeper tank.
python re/msg.py 10560
(reads re/private/strings.pkl, the game's strings: not committed)"""
import os, pickle, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = os.path.join(HERE, "private", "strings.pkl")
    if not os.path.exists(path):
        sys.exit("No re/private/strings.pkl: copy it from E:/zt1-shots/tools/strings.pkl")
    strings = pickle.load(open(path, "rb"))
    # (a copy Ghidra's scripts can read: ZT1_Map fills in a message's text)
    js = os.path.join(HERE, "private", "strings.json")
    if not os.path.exists(js):
        import json
        json.dump(dict((str(k), str(v)) for k, v in strings.items()), open(js, "w"))
    if len(sys.argv) == 2 and sys.argv[1].isdigit():
        print(strings.get(int(sys.argv[1]), "(no text with that id)"))
        return
    words = [w.lower() for w in sys.argv[1:]]
    for i in sorted(strings):
        if all(w in str(strings[i]).lower() for w in words):
            print("%6d  0x%x  %s" % (i, i, str(strings[i])[:110].replace("\n", " ")))


if __name__ == "__main__":
    main()
