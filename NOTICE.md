# Licensing notice

This repository mixes code under different terms. Read this before reusing any of it.

## BottleShip's own changes: MIT

Changes made in this fork are released under the MIT License (see [LICENSE](LICENSE)). That covers
the commits in this repository that are not in
[Waterdish/Shipwright-Android](https://github.com/Waterdish/Shipwright-Android), and the commits on
the `bottleship` branch of [flossbud/libultraship](https://github.com/flossbud/libultraship) that
are not in [Waterdish/libultraship](https://github.com/Waterdish/libultraship).

## Ship of Harkinian and the Android port: no license

The rest of this repository comes from
[HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright) (Ship of Harkinian) by
way of [Waterdish/Shipwright-Android](https://github.com/Waterdish/Shipwright-Android). Neither
project has a license. Ship of Harkinian includes code from the
[ZeldaRET](https://github.com/zeldaret/oot) decompilation of *The Legend of Zelda: Ocarina of Time*.
The decompilation is unlicensed, and the Ship of Harkinian team has said it cannot license its
project until that changes. See [Shipwright #2642](https://github.com/HarbourMasters/Shipwright/issues/2642),
[#4023](https://github.com/HarbourMasters/Shipwright/issues/4023) and
[oot-licensing #1](https://github.com/Dragorn421/oot-licensing/issues/1).

Nothing here grants any rights to that code. All rights stay with its authors.

## Submodules: MIT

| Submodule | Source | License |
|---|---|---|
| `libultraship` | [flossbud/libultraship](https://github.com/flossbud/libultraship), forked from [Kenix3/libultraship](https://github.com/Kenix3/libultraship) via Waterdish | MIT, see `libultraship/LICENSE` |
| `OTRExporter` | [Waterdish/OTRExporter](https://github.com/Waterdish/OTRExporter), forked from [louist103/OTRExporter](https://github.com/louist103/OTRExporter) | MIT, see `OTRExporter/LICENSE` |
| `ZAPDTR` | [Waterdish/ZAPDTR](https://github.com/Waterdish/ZAPDTR), forked from [HarbourMasters/ZAPDTR](https://github.com/HarbourMasters/ZAPDTR) | MIT, see `ZAPDTR/LICENSE` |

Libraries those depend on (SDL2 and others) keep their own licenses.

## Game data and trademarks

This repository contains no ROM and no game assets. The app builds its asset archives (`oot.otr`,
`oot-mq.otr`) on the device from a ROM the user supplies, and those files must never be committed
here. *The Legend of Zelda* and *Ocarina of Time* are trademarks of Nintendo. This project is not
affiliated with or endorsed by Nintendo.
