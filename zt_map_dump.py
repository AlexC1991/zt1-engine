import glob
import zipfile


def norm(s: str) -> str:
    return s.lower().replace("\\", "/")


def main():
    ztds = glob.glob(r"build\Release\*.ztd")

    needle = "freeform/mars"
    want_suffix = "/n"

    found = []

    for p in ztds:
        try:
            with zipfile.ZipFile(p, "r") as z:
                for n in z.namelist():
                    s = norm(n)
                    if needle in s and s.endswith(want_suffix):
                        found.append((p, n))
        except Exception as e:
            print(f"ERROR opening {p}: {e}")

    if not found:
        print("NO N FOUND")
        return

    for p, n in found:
        print(f"{p} :: {n}")


if __name__ == "__main__":
    main()