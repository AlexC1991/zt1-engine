r"""The ZT1 Lab's .ztd editing: a file inside a mod's .ztd read, saved back,
or one of the game's files copied in to be changed (a mod's file with the
game's path takes the game's place while the mod is on).

One JSON request on stdin, one JSON answer on stdout:
  {"op": "read", "ztd": <path>, "entry": "animals/quagga.uca"}
      -> {"ok": true, "text", "crlf"}
  {"op": "write", "ztd", "entry", "text"}          (the entry replaced, or added)
      -> {"ok": true}
  {"op": "copyIn", "ztd", "engineDir", "file": "scenery/building/restrant.ai"}
      -> {"ok": true, "entry", "text", "crlf", "had": true if it was there already}
"""
import json
import os
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402


def find(z, entry):
    want = entry.lower().replace(chr(92), '/')
    return next((n for n in z.namelist() if n.lower().replace(chr(92), '/') == want), None)


def text_of(data):
    t = data.decode('latin1')
    return t.replace('\r\n', '\n'), '\r\n' in t


def write(ztd, entry, data):
    """The .ztd again with the entry replaced (or added), made beside it and
    swapped in whole - a half-written .ztd is never left"""
    os.makedirs(os.path.dirname(ztd), exist_ok=True)
    fd, tmp = tempfile.mkstemp(suffix='.ztd', dir=os.path.dirname(ztd))
    os.close(fd)
    try:
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as out:
            name = entry
            if os.path.exists(ztd):
                with zipfile.ZipFile(ztd) as z:
                    name = find(z, entry) or entry
                    for info in z.infolist():
                        if info.filename != name:
                            out.writestr(info, z.read(info.filename))
            out.writestr(name, data)
        os.replace(tmp, ztd)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def run(req):
    ztd, op = req['ztd'], req.get('op')
    if op == 'read':
        with zipfile.ZipFile(ztd) as z:
            name = find(z, req['entry'])
            if not name:
                raise ValueError('%s isn\'t in %s.' % (req['entry'], os.path.basename(ztd)))
            text, crlf = text_of(z.read(name))
        return {'ok': True, 'text': text, 'crlf': crlf}
    if op == 'write':
        text = req['text']
        if req.get('crlf', True):
            text = text.replace('\r\n', '\n').replace('\n', '\r\n')
        write(ztd, req['entry'], text.encode('latin1', 'replace'))
        return {'ok': True}
    if op == 'copyIn':
        entry = req['file'].lower().replace(chr(92), '/')
        if os.path.exists(ztd):
            with zipfile.ZipFile(ztd) as z:
                name = find(z, entry)
                if name:
                    text, crlf = text_of(z.read(name))
                    return {'ok': True, 'entry': name, 'text': text, 'crlf': crlf, 'had': True}
        data = animaker.Game(req['engineDir']).read(entry)
        write(ztd, entry, data)
        text, crlf = text_of(data)
        return {'ok': True, 'entry': entry, 'text': text, 'crlf': crlf, 'had': False}
    raise ValueError('No op "%s".' % op)


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = run(req)
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, (ValueError, FileNotFoundError)) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
