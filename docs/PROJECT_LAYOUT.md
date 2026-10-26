# One ZT1 project, one separate mod repository

The CodeRook project **zt1-engine** is the home of the engine, Lab, shared
knowledge API, release tooling and release downloads.

| Part | Location |
|---|---|
| Engine | `src/` |
| Lab | `lab-app/` |
| Shared knowledge API | `re/cloud/`, with its Worker configuration and migrations |
| Official installer and release tooling | Owner-only `installer/`, `private/` and the protected release scripts |
| Release downloads | Named releases on `zt1-engine` |
| Historical consolidation records | Owner-only `private/legacy-releases/` |

The knowledge API is deployed from its folder in this project. It does not
need a second source repository. Keeping its running Worker separate from the
desktop executable does not require separate source hosting.

The **zt1 mod repository** is separate: it serves scanned mod uploads and the
signed catalogue used by the game. Consolidating source and build tooling does
not move or reset uploaded mods.

## Public source and private tooling

CodeRook's private-path settings keep official build tooling visible only to
the project's own people. These settings apply across saved versions. The
GitHub checkout continues to omit that tooling through `.gitignore`.

The GitHub mirror retains CodeRook's owner-only paths without fetching their
contents into CI. It still synchronizes public file removals. On the release
machine, `engine-build-resources/save_private.py` updates the protected paths
through a remote workspace in **zt1-engine**, preserving the engine source.

Signing keys stay on the release machine and are never stored in either
repository. A source consolidation does not deploy the API or publish a new
game or Lab release.
