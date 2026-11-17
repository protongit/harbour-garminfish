> [!IMPORTANT]
> ## This is a fork
>
> **GarminFish** (this repository) is a fork of
> [Amazfish](https://github.com/piggz/harbour-amazfish) by
> [piggz](https://github.com/piggz). It is **not** the official Amazfish
> project and it is not maintained by the Amazfish authors.
>
> The fork focuses on **Garmin** support, tested on the **Garmin Instinct 2
> Solar**: FIT import (activities, sleep, stress, SpO2, HRV, body energy),
> sleep stages with the watch's own timings, phone music controls, call
> notifications and pairing recovery.
>
> Upstream remains the right place for general Amazfish support, downloads and
> issues: <https://github.com/piggz/harbour-amazfish>.
>
> See [FORK.md](FORK.md) for the list of changes and build instructions.
> Licensed under **GPL-3.0**, same as upstream. All original copyright belongs
> to the Amazfish contributors.

[![Translation status](https://hosted.weblate.org/widget/harbour-garminfish/svg-badge.svg)](https://hosted.weblate.org/engage/harbour-garminfish/)

# GarminFish

* [Upstream project](https://github.com/piggz/harbour-amazfish)
* [Upstream documentation](https://amazfish.github.io/)
* [Changes in this fork and build notes](FORK.md)

GarminFish is a fork of Amazfish. It keeps the Huami/PineTime support and
adds Garmin support, tested on the Garmin Instinct 2 Solar.

## Garmin support in this fork

Tested on the **Garmin Instinct 2 Solar**. Other Garmin devices using the same
GFDI/FIT protocol may work but are not validated.

* Activity import from the watch: sessions, laps, records, developer fields
  and GPX export.
* Sleep: sleep window, exact stage timings (deep, light, REM, awake), sleep
  score, quality label and weekly charts.
* Daily data: steps, heart rate, stress, body energy, HRV and SpO2.
* Phone music control: track title/artist/album, play/pause, next/previous,
  volume up/down and playback position (timeline and elapsed time).
* Calls: incoming call notification with accept/decline actions.
* Pairing: recovers when the watch is already bonded in BlueZ but the app lost
  its own configuration, plus a "Forget" action in the pairing list.
* Connection: the background service supervises the link, reconnects on its own
  after drops, walks the user through BlueZ losing the device object, and
  re-arms notifications after every reconnection. See "Connection reliability"
  in [FORK.md](FORK.md).

## Installation (Sailfish OS)

This fork is **not published in Chum, the OpenStore or Flathub**. Those
packages install the upstream Amazfish app, not this fork.

Build and install this fork from source with the Sailfish SDK; see
[FORK.md](FORK.md) for the commands.

For the upstream app and its downloads, see
[piggz/harbour-amazfish](https://github.com/piggz/harbour-amazfish).

## License and attribution

This project is distributed under the **GNU General Public License v3.0** (see
[LICENSE](LICENSE)). It is a fork of
[Amazfish](https://github.com/piggz/harbour-amazfish); the original code and
copyright belong to the Amazfish contributors. If you redistribute this fork,
keep the upstream attribution and the GPL notice.
