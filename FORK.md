# GarminFish — fork notes

GarminFish (this repository) is a **fork of
[Amazfish](https://github.com/piggz/harbour-amazfish)** by
[piggz](https://github.com/piggz). It is not the official Amazfish project
and it is not maintained by the Amazfish authors.

Upstream is the place to go for general Amazfish support, releases and issues:
<https://github.com/piggz/harbour-amazfish>.

## What this fork adds

The work in this fork is focused on Garmin support, tested on a **Garmin
Instinct 2 Solar** running the GFDI/FIT protocol.

The tree is based on **upstream Amazfish 3.0.0** (`master` keeps upstream merged
in). Garmin work that upstream has since added (Find My Phone, sleep charts)
is available too; the phone music implementation of this fork is kept because
it also sends the playback position.

### FIT import

* Activities: sessions, laps, records, developer fields, GPX export and a FIT
  inspector page.
* Daily monitoring: steps, heart rate, intensity, stress, body energy, HRV and
  SpO2.
* Sleep: sleep window (onset and wake time), stage timings and sleep scores.
* An import ledger makes repeated syncs idempotent and forces a re-import when
  the parser version changes.

### Sleep

* Garmin timestamps a sleep stage at the **end** of the span it covers. The
  importer stores each stage with that rule and anchors the first span at the
  sleep window onset, so the durations match what the watch shows (total,
  deep, light, REM and awake).
* The sleep page shows the night interval, awake time, the overall score with
  the watch's quality label, recovery, average stress, a night hypnogram and
  weekly score/stage charts.

### Phone music controls

* Track title, artist and album on the watch.
* Play/pause, next/previous and volume up/down from the watch.
* Playback position is pushed once per second so the watch timeline and
  elapsed time work.
* Implemented with the Garmin GFDI music messages (`MUSIC_CONTROL`,
  `MUSIC_CONTROL_CAPABILITIES` and `MUSIC_CONTROL_ENTITY_UPDATE`).

### Calls

* Incoming call notifications with accept and decline actions, matching the
  action layout used by Gadgetbridge.

### Pairing

* If BlueZ already has the watch bonded but the app configuration is empty,
  pairing now connects directly instead of failing with "Already exists".
* The pairing list has a **Forget** action that clears the app pairing (and
  tries to remove the BlueZ bond; on some systems only the settings UI or root
  can remove it).

### Connection reliability

The daemon keeps the watch connected by itself, so notifications keep arriving
when the UI is closed.

* The connection is supervised every 5 seconds. BlueZ `Connected` alone is not
  treated as healthy: a link that does not finish the Garmin handshake in 45
  seconds is torn down and retried.
* Retries back off from 5 to 60 seconds and never give up. A user-initiated
  disconnect or unpair stops them until the next explicit connect.
* If BlueZ loses the device object (for example after a `bluetoothd` restart or
  an adapter reset), the daemon runs a short directed LE scan to make BlueZ
  create it again. Failed scans are retried after five minutes so the radio is
  not scanning continuously.
* The Garmin session is rebuilt when BlueZ resolves the services after the link
  is already up, and the daemon reports `authenticated` on every reconnection.
  Notification forwarding, music state, calendar and sync are re-armed each
  time.
* The watch is marked `Trusted` in BlueZ after pairing.
* Protocol loops are blocked: a status message with `Ack` status is never
  acknowledged (answering it used to start an infinite ping-pong), repeated
  identical frames are dropped, and repeated notification subscription requests
  are answered at most once every two seconds.

The daemon keeps a small rotating log in
`~/.local/share/harbour-garminfish/harbour-garminfish/garminfish-daemon.log`.
By default it stores warnings and connection state changes. For a full protocol
trace, create the marker file and restart the service:

```sh
mkdir -p ~/.local/share/harbour-garminfish/harbour-garminfish
touch ~/.local/share/harbour-garminfish/harbour-garminfish/debug-log
systemctl --user restart harbour-garminfish
rm ~/.local/share/harbour-garminfish/harbour-garminfish/debug-log   # back to normal
```

The application version is shown in **Settings > Application Settings > About**.

## Submodules and vendored code

* `qble` and `daemon/Qt-AES` remain submodules pointing at their upstream
  repositories.
* `daemon/libwatchfish` is **vendored** in this fork. The fork changes it to
  expose the playback position, read the Sailfish volume correctly and report
  volume changes. Keeping it as a submodule would require a separate
  libwatchfish fork just for those small changes.

## Building

The project is built with the [Sailfish SDK](https://sailfishos.org/develop/).

```sh
git clone <this-fork-url>
cd harbour-garminfish

# Only the remaining submodules need to be fetched
git submodule update --init --recursive

# Install the build requirements once in the SDK
sfdk -c target=<sailfish-target> -c specfile=rpm/harbour-garminfish.spec build-requires

# Build for the installed Sailfish target
sfdk -c target=<sailfish-target> -c specfile=rpm/harbour-garminfish.spec build
```

If `build-requires` fails while resolving `bluebinder` or
`/usr/bin/getprop`, install the two devel packages directly in the build
environment and retry the build:

```sh
sfdk ... build-shell --maintain zypper --non-interactive install ninja amber-mpris-devel
```

The RPM is written to `RPMS/`. Install it with:

```sh
devel-su pkcon install-local -y --allow-untrusted --allow-downgrade RPMS/harbour-garminfish-*.rpm
```

After installing, restart both processes (installing an RPM does not restart
them):

```sh
systemctl --user restart harbour-garminfish
# confirm the process is running the version you just installed
grep "Starting harbour-garminfishd" \
  ~/.local/share/harbour-garminfish/harbour-garminfish/garminfish-daemon.log | tail -1
# close and reopen harbour-garminfish-ui
```

## Notes and privacy

* The `docs/` directory is not versioned in this fork. It is kept locally for
  research and includes a Gadgetbridge export with personal health data, so it
  is intentionally ignored and must not be committed.

## License

Same as upstream: **GNU General Public License v3.0** (see [LICENSE](LICENSE)).
Original copyright belongs to the Amazfish contributors.
