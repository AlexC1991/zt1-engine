# Bundled Help

The Help tab reads `renderer/help/library.js`; articles and media work offline in
both checkouts and packaged copies. The renderer folder is already included by
electron-builder, so Help does not depend on the author's tutorial drive.

Edit the Markdown in `docs/` and the article metadata in `catalogue.json`, then run:

```powershell
python lab-app/tools/build-help.py
node --check lab-app/renderer/help.js
```

The build uses Python-Markdown (`pip install markdown`) on the development
machine. End users need no Markdown package. Only safe document markup and
bundled media links are emitted. Missing media fail the build. Images and videos
already live under `renderer/help/media/`; the source Markdown links to them.

The original recorded tutorial documents are preserved as historical lessons.
Their article notices link to the current publishing and preview guides where
the interface or repository behavior has since changed. The earlier overview
video is explicitly marked as an earlier interface.

The three reference entries use `source` paths in the catalogue to compile the
canonical modding, Lab and original-game references on every build. Edit those
original documents rather than maintaining a second copy in this folder.
