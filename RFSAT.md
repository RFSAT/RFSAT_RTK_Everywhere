# RFSAT RTK Rover — ESP32 / RTK Everywhere fork

Fork of [SparkFun RTK Everywhere](https://github.com/sparkfun/SparkFun_RTK_Everywhere_Firmware) for the RFSAT embedded RTK rover.
It targets the SparkFun RTK Postcard: a Quectel LG290P quad-band receiver with an ESP32-PICO-MINI-02.
It also targets RFSAT custom boards that copy the Postcard wiring.

## What this fork adds

### 1. Correction-source failover

The rover takes up to four NTRIP sources in priority order:

- **Source 1** is the primary, set with the `ntripClient*` settings.
- **Sources 2–4** are the alternates, set with `ntripClientAlt1*`, `ntripClientAlt2*` and `ntripClientAlt3*`.

How the rover reacts to failures:

| Event on the active source | Action |
|---|---|
| TCP connect fails, no caster response within 10 s, `banned`/`sandbox` reply, link broken, or no RTCM for 30 s | Moves to the next source after 2 s |
| `401 Unauthorized`, `SOURCETABLE` (unknown mount point), or another caster error | Disables that source until its settings change or the rover reboots, then moves on |
| Every usable source has failed in a row | Applies the stock back-off (15 s, 30 s, 1 min, 2 min, 5 min …), then starts again from the highest-priority source |
| Every source is disabled | Shuts the NTRIP client down (stock behaviour) |
| Running on a lower-priority source for `ntripClientFailbackInterval` seconds (default 600; 0 = never) | Returns to the highest-priority source; if that fails, failover brings the previous one back in about 2 s |

A loss of the internet link itself (Wi-Fi or cellular down) is not treated as a source failure. The stock network-restart logic handles it, and the active source is kept.

**With no internet at all**, the LG290P uses Galileo HAS. This needs `pppMode = 2`, which is the firmware default and is set again by the configurator. HAS gives roughly 20 cm, while RTCM-based RTK has priority whenever corrections arrive.

### 2. RFSAT Rover Configurator

`Firmware/Tools/RFSAT_Configurator.html` is a single web page. Open it locally in Chrome or Edge. It covers:

- Wi-Fi networks
- The four correction sources, with presets
- GGA, failover and fail-back
- HAS fallback
- Bluetooth mode
- The accuracy sentence `$PQTMEPE` to apps (on by default, setting `messageRatePQTM_PQTMEPE`), so apps such as PocketGNSS show the same accuracy as the display. Without it the firmware keeps PQTMEPE to itself and apps fall back to GST, which reads several times larger on the LG290P.

It generates checksummed `$SPSET` commands. You can send them over USB (Web Serial), or copy them into a Bluetooth terminal. Over Bluetooth, command mode is entered with `+++` (no line ending, after 2 s with nothing sent), then Enter (main menu), then `+` (reply `COMMAND MODE`); over USB, Enter then `+`. It can also save the settings as a profile JSON to reuse on the next unit.

Every field and button shows a hint when hovered (or focused with the keyboard). The **? Help** button (or `?` / F1) opens searchable help on the common problems: entering command mode over Bluetooth and the terminal's line ending, the rover restarting when USB is opened, replies scrolling too fast, the app's accuracy differing from the display, RTK Fixed dropping to DGNSS when two correction streams arrive, Bluetooth pairing in the phone's settings, where the profile JSON is, how to read the firmware version, what C: and M: on the display mean, which firmware file goes on which unit, and choosing a correction source. Quick buttons ask the rover for the firmware versions and whether `$PQTMEPE` is on.

The same settings also appear in the device's own Wi-Fi config page: **GNSS Configuration → Enable NTRIP Client → Correction Source Failover**.

### 3. RFSAT branding, version and display

- **Boot screens** (from rfsat-v3.4-r3): the first shows RFSAT LIMITED (`icons_rfsat.h`, made by `Firmware/Tools/rfsat_logo.py`, which can also convert an image), the second RFSAT Limited, RTK, Postcard and the firmware version.
- **Firmware version**: `v3.4-r<n> <commit>`, e.g. `v3.4-r4 96c1dc8`, on the second boot screen, at the top of the main menu ("RFSAT firmware … (built …)") and appended to `SPGET,espFirmwareVersion`. The build workflow writes `rfsat_version.h` from the release tag (`rfsat-v3.4-r4` gives `r4`); other builds show `dev`.
- **Brand** `BRAND_RFSAT` for the Postcard: the Bluetooth name is `RFSAT Postcard-xxxxxx` (re-pair after updating from a SparkFun-branded build). No setting changed, so updates at 0x10000 keep the settings.
- **Caster and mount point on the rover screen** (from rfsat-v3.4-r4, 128x64 display): two rows between the header icons and the accuracy row while the rover's own NTRIP client is connected: `C:` the caster (by name for known casters: gr.nrtk.eu Metrica SmartNet, euref-ip.net EUREF-IP, rtk2go.com RTK2go, caster.centipede.fr Centipede; any other by its address) and `M:` the mount point; 21 characters each, longer text is cut. The accuracy / satellite row moves down 8 pixels (10-pixel row pitch). Empty while the client is not connected and when a phone relays corrections over Bluetooth. The names are in `rfsatKnownCasters[]` in `Display.ino`.

### 4. Build and CI

`.github/workflows/rfsat-build.yml` does the following:

- Runs the host unit test of the failover logic.
- Builds the firmware with the upstream Dockerfile. ESP32 core 3.0.7 and the libraries are pinned, and no SparkFun secrets are needed.
- Uploads `RTK_Everywhere_Firmware_RFSAT_v3_4_<commit>_V4.bin` (the RTK Firmware Uploader needs "RTK_Everywhere_Firmware" in the name to write the right bootloader), the merged image `…_merged_V4.bin`, the `.elf`, and `RTK_Everywhere_bootloader_V4.bin` / `RTK_Everywhere_partitions_V4.bin` as a workflow artifact, and to the release when a tag is given.
- File names end with the RFSAT variant ID `_V4` (RTK Postcard + Portability Shield), so each file shows which prototype it belongs on.

## Recommended source order

**First tests (Oct 2026):** Metrica MSM_NEAR as source 1, MSM_VRS as source 2, EUREF as source 3. The reference to match is 6 mm, measured with the EOS Arrow Gold on MSM_NEAR. The configurator uses this order by default. Once a base of your own is running, move it to source 1, as in the table below.

Use sources within 2–4 km of the work area for sub-centimetre fixes. The error is about 0.8 cm + 1 ppm × baseline.

| # | Source | Example | Notes |
|---|---|---|---|
| 1 | Own base: EOS Arrow Gold, or DJI D-RTK 2 in its RTCM-out mode, via its caster | `your-caster:2101/OWN1` | Shortest baseline, best accuracy |
| 2 | Metrica VRS | `gr.nrtk.eu:7801/MSM_VRS` | Needs GGA (on) |
| 3 | Metrica nearest station | `gr.nrtk.eu:7801/MSM_NEAR` | Needs GGA |
| 4 | EUREF / nearest public station | `euref-ip.net:2101/<MOUNT>` | Baseline may be long; float or decimetre is likely |

Check host, port and mount point names against your Metrica and EUREF licence sheets.

## Flashing

1. Download `RTK_Everywhere_Firmware_RFSAT_v3_4_<commit>_V4.bin` from the release (or the GitHub Actions run → *RFSAT build* → artifact).
2. Flash it with the [SparkFun RTK Firmware Uploader](https://github.com/sparkfun/SparkFun_RTK_Firmware_Uploader): select the COM port and the `.bin`, then press **Upload Firmware**. The uploader writes the bootloader and partitions itself.
   - Alternatively, use esptool with the single merged image (bootloader, partitions and app at offset 0x0):
     ```
     esptool.py --chip esp32 --baud 921600 write_flash 0x0 RTK_Everywhere_Firmware_RFSAT_v3_4_<commit>_merged_V4.bin
     ```
3. The first boot after flashing resets the settings to defaults, because the settings layout changed. Configure the rover again with the configurator.

## Bench and field test checklist

1. **Boot.** On the USB serial console at 115200, the rover should report the LG290P and its firmware version.
2. **Single source.** Configure source 1 only. Within about 1 minute of a fix, check that `NTRIP Client connected` appears and that RTK float, then fix, follow.
3. **Failover on a wrong mount point.** Set source 1 to a non-existent mount and source 2 to Metrica VRS. Expect `failover (unknown mount point): source 1 -> source 2` and a fix on source 2.
4. **Failover on data loss.** While on source 1 (own base), stop the base's caster stream. Within about 32 s, expect `failover (no RTCM data)` and a fix again on source 2.
5. **Fail-back.** Restore the base. Set `ntripClientFailbackInterval` to 120 for the test, and expect a return to source 1 within 2 minutes.
6. **Back-off.** Make all sources unreachable (a wrong host on each). The rover should cycle once, then follow the 15 s → 30 s → 1 min back-off.
7. **No internet.** Switch off Wi-Fi. Within minutes the LG290P should report PPP/HAS (about 20 cm) instead of RTK. When Wi-Fi returns, RTK should come back.
8. **Bluetooth.** Pair an Android phone (SPP) and check NMEA in a GNSS app (e.g. SW Maps with an external receiver). An iPhone should connect over BLE.
9. **Availability log.** Log GGA for at least 2 hours in each environment. Compute the share of epochs with fix quality 4 and horizontal error < 1 cm against a surveyed point. The target is ≥ 50 % now and 95 % eventually.

In the main serial menu, `s` (System) prints the status, including the NTRIP source table: active source, disabled sources and the switch count.
