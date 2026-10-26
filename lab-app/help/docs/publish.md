# Publish, download and update mods

## Prepare one mod

Save the assets and map, then open **About**. Set the name, description, version and picture. Explain inherited art or behavior and known limits. For an update, increase the version and keep the same repository ID.

## Combine a theme pack

Choose **Publish → Build a theme pack…** and select two or more compatible mods. Give the pack its own name, version, description and image. The builder reports conflicting files and includes identical shared files once. Keep the original mods: the pack is a snapshot, not a live link to future source edits.

Check and upload the resulting pack as one mod. Players should enable the pack with its component mods off to avoid duplicate definitions or replacements. Mods with identity-specific settings may need to stay separate.

## Check, upload and wait

**Check it** validates the local package. **Upload it** uses your signed-in CodeRook account and sends the package to the repository. Wait for its own rules and malware scan to finish, then confirm **Your uploads** reports that it is in the repository. A local check alone does not publish it.

The current Lab generates a small repository thumbnail automatically from the About picture. It travels inside the checked package. The game shows that cover before installation. Older published packages gain this preview after a new upload through the current Lab; a listing without a valid picture remains downloadable.

## Retrieve it in the actual game

1. Open **Game Options → Mods & Updates → Mod Menu → Mod repository**.
2. Select the mod or pack. Read its description and preview its cover.
3. Choose **Download** and wait for completion.
4. Open **Your mods**, switch it on and restart the game. A new download starts off.
5. In the Lab's embedded Play, stop and start Play to reload mods.

For mod maps, enable **custommaps** and look in Freeform. For the tutorial theme, keep the three separate component mods off.

## If Windows cannot replace an update

A running game or Lab host can hold its archive open. Switch the mod off and restart Play and the Lab so the archive is unloaded, then retry the download. Keep a copy of editable creator sources before replacing a locally edited version. The Help instructions do not remove or overwrite those sources for you.

## Local ZIP imports

Use the game's **Add a mod…** picker for a supplied installable ZIP. This is separate from public publishing. For public sharing, use Publish and wait for repository approval.

[Actual publishing checkpoints](help:publication-tutorial) · [Repository pictures and clean download proof](help:preview-tutorial)
