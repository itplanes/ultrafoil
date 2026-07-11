# UltraFoil

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

UltraFoil only installs exact Build ID matches. Cheats published for another build are shown as unavailable and are not renamed or installed.

## Build

UltraFoil uses devkitPro, libnx, and Plutonium.

```sh
make -j$(nproc)
```

Build output: `ultrafoil.nro`.

## License

GPL-3.0. UltraFoil retains the licensing and attribution requirements of the projects it derives from.

The running-game Build ID lookup is based on the GPL-3.0 AIO-Switch-Updater dmnt flow. See `aio-switch-updater` for its original authors and notices.
