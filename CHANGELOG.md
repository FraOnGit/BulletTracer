# Changelog

## 1.0.2

- Works with the game update 3.290.1 and ZHMModSDK 4.2.1. The update moved the game code the mod hooks, so 1.0.1
  stayed off ("Not active") on the new version.
- The DLL no longer contains the folder it was built in.

## 1.0.1

- Fixed: restarting the mission with infinite ammo on left the ammo infinite, with no way to turn it off.
  Infinite ammo now turns off on every restart or level change.
- The **Infinite ammo** checkbox shows the game's actual state: ammo left infinite by something else (such as an
  older version, or another mod) shows as ticked and can be turned off by unticking it.

## 1.0.0

First release.

- Traces darts (or every shot, with **Trace all shots**), with depth rendering, line color and **Clear the lines**.
- Infinite ammo toggle.
- Darts keep being traced after pausing and resuming the game in FreeCam.
