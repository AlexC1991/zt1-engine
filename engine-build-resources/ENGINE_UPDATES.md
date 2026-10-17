# ZT1 Engine updates

The installed engine accepts a release feed only when `latest.yml` contains a
valid ECDSA P-256 signature over the app id, version, installer name, file
size, and SHA-512. It verifies the same size and SHA-512 after download before
starting the installer. The game reads the feed from GitHub's latest release
only: CodeRook mirrors each release, but its update feed isn't reliable yet
(it can offer a version before its installer is attached), so `src/Updater.cpp`
leaves it out until it is. Attach the signed `latest.yml` to both anyway;
unsigned feeds are rejected.

The release workflow needs the repository Actions secret
`ZT1_ENGINE_UPDATE_KEY`, containing the PKCS#8 PEM private key. The matching
private key for this checkout is kept outside the repository at
`%APPDATA%\zt1-engine-release\update-key.pem`. Never commit or share that
key. The public key is pinned in `src/Updater.cpp`. A tagged release fails at
the signing step if the secret is missing or invalid.

Add `ZT1_ENGINE_UPDATE_KEY` under the repository's Settings → Secrets and
variables → Actions, using the contents of the local PEM key. The tag workflow
signs the generated `dist/latest.yml` before attaching it to GitHub and the
CodeRook release.

For a local release build, sign the generated feed with:

```text
node engine-build-resources/sign_engine_update.js <version> dist/ZT1-Engine-Setup-<version>.exe dist/latest.yml
```
