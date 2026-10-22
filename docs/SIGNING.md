# Signed update packages

Every release's `quark-<platform>.qpak` is signed in CI and published with a `quark-<platform>.qpak.sig` next to it.
The launcher downloads both and refuses to install a package whose signature doesn't verify against the public key
built into it (`kReleasePublicKey` in `src/common/sign.cpp`). Server autoupdate (`tools/server/autoupdate.sh`) checks
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

The release job in `.github/workflows/release.yml` **fails if the `QUARK_SIGNING_KEY` secret is missing**, or if the
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
can move it only when `master.txt.sig` next to it is a release-key signature of exactly that file:

```
echo https://new-worker.example.workers.dev > site/master.txt
QUARK_SIGNING_KEY=<private key> build/quark_sign sign site/master.txt   # writes site/master.txt.sig
build/quark_sign verify site/master.txt                                  # checks it against the built-in key
git add site/master.txt site/master.txt.sig
```

Do this on your own machine, not in CI: CI runs for pushes to any branch, so a signature made there would vouch for
whatever a branch put in `master.txt`. The pages job only has to publish `site/` as it is (the `.sig` included).

## Making a new key (only if the old one leaked or was lost)

1. Build and run `build/quark_sign keygen`.
2. Put the printed **public** key in `kReleasePublicKey` in `src/common/sign.cpp`, commit.
3. Replace the `QUARK_SIGNING_KEY` secret with the printed **private** key (steps 2–3 above).
4. Note: launchers already installed only trust the old key. Players must download the new launcher once
   (from the website / release page) to get updates signed with the new key.

## Notes

- Releases published before signing have no `.sig`; the launcher refuses to install them ("the release has no
  signature file"), so players can't roll back to those versions through the launcher.
- Never paste the private key into an issue, commit, log or chat. If you think it leaked, make a new key.
