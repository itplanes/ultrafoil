# UltraFoil

![Regression checks](https://github.com/itplanes/ultrafoil/actions/workflows/ci.yml/badge.svg)

UltraFoil is a lightweight Nintendo Switch homebrew client designed to run alongside CyberFoil and connect to an AeroFoil server.

## Features

- Install content from an AeroFoil Remote.
- Install content from a URL or a LAN sender.
- Back up, browse, restore, and remove save-data backups through AeroFoil.
- Find cheats for the exact running Title ID and Build ID through AeroFoil.
- Install and remove Atmosphere cheat files with automatic local backups.

SD, USB, USB HDD, and MTP browsing/install entry points are intentionally not exposed by UltraFoil.

## Coexistence with CyberFoil

UltraFoil uses separate application files and does not reuse CyberFoil's configuration directory:

- Executable: `switch/UltraFoil/ultrafoil.nro`
- Configuration and cache: `sdmc:/switch/UltraFoil/`
- Atmosphere cheats: `sdmc:/atmosphere/contents/<TITLE_ID>/cheats/<BUILD_ID>.txt`
- Replaced cheat files are backed up below `sdmc:/switch/UltraFoil/cheat_backups/`.

## AeroFoil setup

Configure an AeroFoil Remote in Settings. Cheat lookup and save-data management use the same server URL and credentials.

The AeroFoil server provides:

- `GET /api/cheats/titles/<title_id>/builds/<build_id>`
- `POST /api/cheats/render`

The cheat manager distinguishes actual running BID matches, version recommendations, missing evidence and incompatible builds. Offline recommendations use the installed internal version and require confirmation; they do not verify the actual BID. Incompatible/unknown builds can be inspected but cannot be installed. Build IDs are never renamed to force a match.

The manager has separate game/build/code lists and a detail panel. `A` selects codes, `X` installs the selection, `Y` removes a managed selection, `L/R` scroll details, `+` opens diagnostics and `-` opens recent cheat and content installation history. Queries run in the background and `B` cancels them. Conflicting selections are named explicitly and must be resolved before installation. One unambiguous master code is included automatically.

Installations use a two-file recovery journal for payload and ownership metadata. Updating creates backups; removal verifies the installed hash and restores the original replaced file when available. External/edited files are protected. Legacy selections without ownership hashes require a backed-up replacement before managed removal is available. Installing a file does not verify whether an overlay has enabled it.

The updated client requires AeroFoil client API v2 (`GET /api/client/status` and `GET /api/cheats/titles/<title_id>/index`). HTTP range retries continue at the received offset and validate `Content-Range` before forwarding bytes; this is reconnect continuation within the current installation, not recovery of an installation after restarting the app. Optional application update reminders use only the UltraFoil release repository.

## Build

UltraFoil uses devkitPro, libnx, and Plutonium.

GitHub Actions pins the verified devkitPro image digest rather than following `latest`.

```sh
make -j$(nproc)
```

Build output: `ultrafoil.nro`.

## License

GPL-3.0. UltraFoil retains the licensing and attribution requirements of the projects it derives from.

The running-game Build ID lookup is based on the GPL-3.0 AIO-Switch-Updater dmnt flow. See `aio-switch-updater` for its original authors and notices.
