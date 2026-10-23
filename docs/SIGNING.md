# Signed update packages

From 1.17.0 on **everything published is signed** with the release key (owner decision B6): every release asset
(production and testing), the launcher itself, and the website. Releases up to 1.16.x came before signing (see the
end of this page).

| What | Signature | Checked by |
|---|---|---|
| `quark-<platform>.qpak` (update package) | `quark-<platform>.qpak.sig` | the launcher before installing; server autoupdate |
| `quark-release.txt` (signed manifest, below) | `quark-release.txt.sig` | the launcher; server autoupdate |
| the launcher inside each package | `quark_launcher[.exe].sig` inside the package, next to it | the launcher's self-update and the game's launcher update: a packaged launcher is only ever put in use when it verifies |
| `quark-launcher-windows.exe`, `quark-launcher-linux`, `quark-launcher-macos.zip` (the website's downloads) | `<file>.sig` | the player (`quark_sign verify <file>`); also named in the signed manifest |
| `quark-<platform>.zip` / `.tar.gz` (game only, no launcher) | `<file>.sig` | the player; also named in the signed manifest |
| `SHA256SUMS` (every asset and signature of the release) | `SHA256SUMS.sig` | the player |
| the website (`index.html`, `changelog.json`, `weapons.json`, images...) | `SHA256SUMS` of every site file + `SHA256SUMS.sig`, made by the pages job | the player |
| `site/master.txt` | `master.txt.sig` over a prefix, made by the owner (below) | the game |

Every release's `quark-<platform>.qpak` is signed in CI and published with a `quark-<platform>.qpak.sig` next to it.
The launcher downloads both and refuses to install a package whose signature doesn't verify against the public key
built into it (`kReleasePublicKey` in `src/common/sign.cpp`), or that the release's signed manifest (below) doesn't
name. Server autoupdate (`tools/server/autoupdate.sh`) checks
the Linux package the same way before it installs a new version.

- Algorithm: Ed25519ph (RFC 8032, SHA-512 prehash) via vendored [Monocypher](https://monocypher.org) 4.0.2
  (`third_party/monocypher`, BSD-2-Clause / CC0).
- Keys and signatures are hex text: private key 64 hex digits (32-byte seed), public key 64, signature 128.
- Tool: `quark_sign` (built with everything else: `cmake --build build --target quark_sign`).

```
quark_sign keygen                    # new keypair (prints private + public)
quark_sign pubkey < key.txt          # public key of a private key
quark_sign sign <file>               # key from $QUARK_SIGNING_KEY (or stdin); writes <file>.sig
quark_sign verify <file> [pubkey]    # checks <file>.sig; default key = the one built into the launcher
```

Each release (production and testing) also carries `quark-release.txt` + `.sig`: a signed manifest naming the
channel, tag, version and commit, and the sha256 of every package (`tools/sign/release_manifest.sh`):

```
quark release
channel testing
tag test-1.17.0-test.3
version 1.17.0-test.3
commit <40 hex>
sha256 quark-linux.qpak <64 hex>
sha256 quark-windows.qpak <64 hex>
```

Dedicated servers check it before building, so they only ever build the exact commit a signed production release
names. The launcher checks it before installing: the package's own `.sig` only proves the release key signed *some*
package, so the manifest must also verify and name this release's tag, version and channel (a prerelease = testing)
and the downloaded package's sha256, or the install is refused (a validly signed old or testing package can't be
passed off as another release or channel). A testing build's macOS package is added after the prerelease went out, so
it comes with its own signed `quark-release-macos.txt`, which the launcher prefers over `quark-release.txt` when
present. Releases from before signing (<= 1.16) have neither and stay installable (see the end of this page).

Signing runs in the `release` / `release-testing` / `release-testing-macos` jobs of `.github/workflows/release.yml`
(tools/sign/sign_release.sh), in this order: the launcher inside each package is signed and its `.sig` is added to the
package (`sign_release.sh --launchers`, tools/sign/qpak_launcher_sig.py: it changes the package, so it comes first),
then the manifest is made (naming every download), then every published file gets its `.sig`, then `SHA256SUMS` is
made and signed. The `pages` job signs the website's `SHA256SUMS` the same way. All of it runs
inside the GitHub environments `production` and `testing`. The secret can be a repository secret, or (better) an
environment secret on those two environments with a deployment rule (production: tags `v*`; testing: branch
`testing`), so no other branch's workflow can read it.

The release jobs **fail if the `QUARK_SIGNING_KEY` secret is missing**, or if the
secret doesn't match the public key in the source, so no unsigned release goes out.

## Owner: one-time setup

A keypair has already been generated for you and its public key is in `src/common/sign.cpp`. The private key was
written (outside the repo, never committed) to the session scratchpad file `quark_signing_key.txt`.

1. Copy the private key (the 64 hex digits in `quark_signing_key.txt`) somewhere safe, e.g. a password manager.
   Then delete that file.
2. On GitHub open **floration/quark → Settings → Secrets and variables → Actions → New repository secret**.
3. Name: `QUARK_SIGNING_KEY`. Secret: the 64 hex digits (nothing else). Click **Add secret**.
4. Push as usual. The release job's "sign update packages" step should print `signed ...` and `signature OK` for each
   platform, and the release should list a `.qpak.sig` next to each `.qpak`.

## Moving the ranking / server-list Worker (signed master.txt)

The Worker's URL is built into the game (`kPinnedMasterUrl`, `src/game/master.cpp`). The website's `master.txt`
can move it only when `master.txt.sig` next to it is a release-key signature of `quark-master-v1\n` followed by
exactly that file (and the URL is `https://`). The prefix keeps the two kinds of signature apart: a signature made
for a package or a release manifest (`quark_sign sign`) never passes as a master.txt signature, so use
`sign-master`, not `sign`:

With the `site` branch checked out in `site/` (README "Branches"):
```
echo https://new-worker.example.workers.dev > site/master.txt
QUARK_SIGNING_KEY=<private key> build/quark_sign sign-master site/master.txt   # writes site/master.txt.sig
build/quark_sign verify-master site/master.txt                                  # checks it against the built-in key
git -C site add master.txt master.txt.sig && git -C site commit -m "master.txt: move the Worker" && git -C site push
```

The Discord bot doesn't read master.txt (it sends BOT_KEY to the Worker, so it only uses an address its operator
configured): when you move the Worker, also set `master_url` in the bot's `config.json` (or `QUARK_MASTER` in
`/etc/quark-discord.env`) and restart it.

Do this on your own machine, not in CI: CI runs for pushes to any branch, so a signature made there would vouch for
whatever a branch put in `master.txt`. The pages job only has to publish `site/` as it is (the `.sig` included).

## Making a new key (only if the old one leaked or was lost)

1. Build and run `build/quark_sign keygen`.
2. Put the printed **public** key in `kReleasePublicKey` in `src/common/sign.cpp`, commit.
3. Replace the `QUARK_SIGNING_KEY` secret with the printed **private** key (steps 2–3 above).
4. Note: launchers already installed only trust the old key. Players must download the new launcher once
   (from the website / release page) to get updates signed with the new key.

## Release channels and launcher updates

- **Production** = normal GitHub releases, tag `v<VERSION>`. **Testing** = GitHub *prereleases*, tag
  `test-<VERSION>-test.<N>` (e.g. `test-1.17.0-test.3`; the launcher reads the version from the first digit on).
  Testing tags must never start with `v<digit>` or a digit: launchers 0.12 - 1.16 install the highest
  `v<digit>`/digit tag they see, prerelease or not. Both channels use the same asset names
  (`quark-<platform>.qpak` + `.qpak.sig`) and are signed with the same key; drafts are never offered.
- The launcher reads **every page** of the GitHub releases list (100 per page, until a short page), so production
  releases stay listed however many test builds come after them, and no release is ever pruned: every version,
  production and testing, stays installable (owner decision). If a later page fails, what was read is shown and a
  pinned version that isn't in it is left alone (nothing is installed).
- The launcher's CHANNEL button switches the version list (remembered in `launcher.cfg`). On its own it only ever
  installs a *newer* version than the installed one; an older one only when the player picks it. LATEST is always
  the newest *signed* release.
- Every qpak carries the launcher (`quark_launcher[.exe]` next to the game) and, from 1.17.0 on, its signature
  (`quark_launcher[.exe].sig`, added by the release job). Neither the launcher nor the game ever puts a packaged
  launcher in use unless that signature verifies against the release key (the copy that goes in place is the one
  checked). A launcher that installs a package
  with a newer launcher swaps itself (POSIX: rename over; Windows: running exe -> `.old`, new one in, `.old` deleted
  next start) and restarts once. The game, at startup, replaces every older launcher it finds (the folder above
  its game folder, the install folder, `%LOCALAPPDATA%\quark` from before 0.12) and repoints the shortcuts, so
  players on any earlier launcher get the new one through a normal game update. The version is read from a
  `QUARK-LAUNCHER-VERSION:<VERSION>` marker in the binary (launchers without it count as older). Installing an
  older package (picked by hand, signed or not) never downgrades the launcher: only a newer marker replaces it.
- **Old files on update** (owner decision C13, `src/launcher/gamefiles.h`): the launcher writes
  `installed-files.txt` into the game folder at every install (each file the package put there, with its sha256 and size), and
  every package from 1.17.0 on carries the same list (`tools/pack.py`), so installs made by an older launcher have it
  too. An update deletes only *retired game files*: listed, unchanged since (same sha256), not in the new package, and
  not a player file (`*.cfg`, `*.key`, demos, screenshots, logs, light caches, downloaded maps, the player's folders).
  Everything else of the old install is carried over. An install with no list (a package from before 1.17.0 installed
  by an older launcher) keeps every file: nothing is known to be retired.
- **Demos on update** (`src/game/demo.h`): at the first start after an update the game converts the demos an older
  version recorded (on a background thread), keeping each original under `demos/old/<version>/`. Every protocol
  since the first demos (24, 1.11.0) records the same way, so they convert by checking every record and restamping;
  one that doesn't convert (a newer or unknown version, a map this version can't load) stays as it is, is logged in
  `demos/old/convert.log`, and the demo list shows "recorded with version X". The game notes in `versions.txt` (user
  data) which version ran which protocol, so later versions can name the version that recorded an old demo.
- The game's launcher update runs on a background thread that the game stops and joins when it quits: no new
  step (a launcher swap's rename pair, the shortcuts, the programs entry) starts after quitting began, the one
  running finishes, and the game waits at most 2 s for the thread. Automated runs (`--screenshot`, `--bench`,
  `--edscript`, `--textheavy`, `--menushot`, `--chatshot`, `--consoleshot`, `--perf`) don't start it at all.

## Rolling back to a version from before signing

Releases published before signing (every release up to 1.16.x) have no `.sig`. They are listed like any other
version, with no warning and no extra confirmation (owner decision B6), so any version stays playable:

- Only versions below 1.17.0 can be unsigned. A release without a `.sig` that claims 1.17.0 or later (including
  1.17.0 test builds) can't be genuine: it is not listed at all, and the launcher refuses to install it.
- They are **never installed over a newer version automatically**: LATEST only ever means the newest signed
  release, so following the newest never lands on one.
- A player installs one by picking it in the version list and pressing `INSTALL <version>`. The package is then
  installed as it comes from the GitHub release over HTTPS (there is nothing to verify). Signed releases are always
  verified; a signed package that fails its check is refused as before.
- An old `launcher.cfg` pinned to such a version keeps its pin (it is no longer treated as "latest", so the launcher
  doesn't silently move that player to the newest). If that version is installed, it just plays; if nothing or an
  older version is installed, it is installed; if a newer version is installed, the launcher waits for the player's
  INSTALL instead of replacing it.
- Old packages may carry no launcher (or an older, unsigned one, which never replaces the current launcher) and
  speak older network protocols, so online play may need servers of the same version.

## Antivirus false positives (Windows)

The Ed25519 signature above proves a package came from the release key; Windows doesn't know about it. To Windows the
exes are unsigned, and an antivirus (Windows Defender's machine-learning detections, names like `Wacatac` or
`Trojan:Win32/...!ml`) now and then flags one particular build of an unsigned exe on some PCs and quarantines it,
which deletes it from the game folder (1.15.1 and 1.16.0's `quark.exe` on one player's PC: every other build, and those
builds on everyone else's PCs, were fine).

- **What the launcher does about it (1.17.0 on).** Every file of a new version is read back and checked against the
  package before the swap: one an antivirus took, changed or blocked stops the update with the installed version
  untouched, and the player sees which file, why that most likely happened (with the Windows Security steps to
  restore it and to add an exclusion for the quark folder) and RETRY. Before every start the installed files are
  checked (existence and size; CHECK FILES: sha256): a missing or changed `quark.exe` is never started, the player
  gets the same explanation, OPEN FOLDER and REPAIR (that version downloaded, checked and swapped in again).
- **Clearing a flagged build.** Submit the flagged exe (or the release's `quark-windows.zip`) as a false positive at
  https://www.microsoft.com/wdsi/filesubmission ("Software developer", the detection name from the player's Protection
  history). Microsoft usually clears it within a day or two and the next definition update stops the detections for
  everyone; a player can then restore the file from Protection history or press REPAIR.
- **Making it rarer.** Every exe carries a version resource (product quark, company floration, the version) and an
  application manifest (src/win/, CMakeLists.txt `quark_win_resources`), and exes are never packed or compressed (UPX
  and the like are a strong malware signal): anonymous, packed binaries are what the heuristics flag most.
- **What really prevents it: Authenticode.** Signing `quark.exe`, `quark_server.exe` and `quark_launcher.exe` with a
  code-signing certificate (an OV/EV certificate, or Azure Trusted Signing) gives the files a publisher reputation;
  Defender and SmartScreen then treat new builds from that publisher as known. Not done by the owner's choice (the
  package signature above covers the launcher's own checks); it would be one `signtool`/`osslsigncode` step in the
  Windows job before `tools/pack.py`.

## Notes

- Never paste the private key into an issue, commit, log or chat. If you think it leaked, make a new key.
