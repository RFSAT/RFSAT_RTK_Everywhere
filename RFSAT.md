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

It generates checksummed `$SPSET` commands. You can send them over USB (Web Serial), or copy them into a Bluetooth terminal; send `----------` first to enter command mode. It can also save the settings as a profile JSON to reuse on the next unit.

The same settings also appear in the device's own Wi-Fi config page: **GNSS Configuration → Enable NTRIP Client → Correction Source Failover**.

### 3. Build and CI

`.github/workflows/rfsat-build.yml` does the following:

- Runs the host unit test of the failover logic.
- Builds the firmware with the upstream Dockerfile. ESP32 core 3.0.7 and the libraries are pinned, and no SparkFun secrets are needed.
- Uploads `RFSAT_v3_4_<commit>.bin`, the `.elf`, and the bootloader and partition binaries as a workflow artifact.

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

1. Download the firmware `.bin` from the GitHub Actions run (Actions → *RFSAT build* → artifact).
2. Flash it with the [SparkFun RTK Firmware Uploader](https://github.com/sparkfun/SparkFun_RTK_Firmware_Uploader): select the COM port and the `.bin`, then press **Upload Firmware**. The uploader writes the bootloader and partitions itself.
   - Alternatively, use esptool with the single merged image (bootloader, partitions and app at offset 0x0):
     ```
     esptool.py --chip esp32 --baud 921600 write_flash 0x0 RFSAT_v3_4_afffebb_merged_0x0.bin
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
