# ZT1 Lab updates

The installed Lab checks GitHub Releases for `ZT1-Lab-update.json`. Before
offering an installer it verifies the manifest with the Ed25519 public key
pinned in `lab-app/update.js`; after download it verifies the signed file size
and SHA-512. Users choose whether to install.

The release workflow needs the repository Actions secret
`ZT1_LAB_UPDATE_KEY`, containing the private PKCS#8 PEM key. The matching
private key for this checkout is kept outside the repository at
`%APPDATA%\zt1-lab-release\update-key.pem`. Never commit or share that key.
The public key in `lab-app/update.js` must match the secret. A release tag
build fails at the signing step if the secret is absent or invalid.

To configure the workflow, add `ZT1_LAB_UPDATE_KEY` under the repository's
Settings → Secrets and variables → Actions, then paste the contents of the
local PEM key. Each tagged Lab release builds a version-matched installer and
attaches its checksum and signed manifest to the GitHub release. The Lab
downloads the matching setup asset from that same latest release.

For a local installer, run `node engine-build-resources/sign_lab_update.js
<version> dist/ZT1-Lab-Setup-<version>.exe dist/ZT1-Lab-update.json` after
packing. The script uses the same local private key by default.
