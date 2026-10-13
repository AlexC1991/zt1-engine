"""Fetch the standard licence texts that a package declares but does not ship.

unicorn's wheel declares GPLv2 in its metadata and contains no licence file.
The GPL asks that a copy of the licence travel with the program, so the text
is kept here, from GNU's own canonical copy of it.
"""
import hashlib
import io
import os
import urllib.request

DEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "licenses")
os.makedirs(DEST, exist_ok=True)

WANT = [
    ("GPL-2.0.txt", "https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt",
     "GNU GENERAL PUBLIC LICENSE", "Version 2, June 1991"),
]

for name, url, must1, must2 in WANT:
    out = os.path.join(DEST, name)
    if os.path.exists(out) and "--force" not in os.sys.argv:
        print(f"[=] {name} is already here")
        continue
    print(f"[1/1] {url}")
    req = urllib.request.Request(url, headers={"user-agent": "zt1-lab-build/1.0"})
    data = urllib.request.urlopen(req, timeout=120).read()
    text = data.decode("utf-8", errors="replace")
    if must1 not in text or must2 not in text:
        raise SystemExit(f"[X] {url} did not look like the licence it should be")
    io.open(out, "w", encoding="utf-8", newline="\r\n").write(text)
    h = hashlib.sha256(data).hexdigest()
    print(f"      {len(data)} bytes, sha256 {h[:16]}...")
    print(f"      wrote {out}")
