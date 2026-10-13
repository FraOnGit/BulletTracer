# Bullet Tracer

A [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) mod for **HITMAN World of Assassination** that draws a 3D line
along the path of every shot.

Darts (Sieker, tranquilizer pistol and the like) are real projectiles that fly at about
50 m/s and drop with distance. Bullet Tracer shows exactly where they went, so you can see the arc and learn to aim
at long range.

## Features

- A permanent line for every dart fired, until you clear it.
- **Trace all shots**: off (default) draws only darts; on draws every shot the game fires as a projectile.
- **Use depth rendering**: lines hidden behind walls (on) or always visible on top (off).
- **Infinite ammo** toggle, to practice without reloading. It turns off again when you untick it or load a new level.
- Line color, a **Clear the lines** button, and an optional window that stays visible with the mod menu closed.
- Works with FreeCam

## Requirements

- HITMAN World of Assassination (tested on version 3.280.0, Steam).
- [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK/releases) 4.1.1.

## Install

1. Install ZHMModSDK.
2. Copy `Retail/mods/BulletTracer.dll` from the zip into `HITMAN 3/Retail/mods/`.
3. Start the game, open the SDK menu (`~` key), click **MODS**, tick **BulletTracer** and press **OK**.
4. In the SDK menu bar click **BULLET TRACER** to open the window.

**Uninstall:** disable it in the Mods list, or delete `BulletTracer.dll` (and `bullettracer.ini`, its settings) from
`Retail/mods/`.

## How it works

Each shot in the game is a `ZHM5Projectile`. The mod swaps one method of that class (the one that sets a new shot
up) for its own, which calls the original and only notes the projectile's address. From then on the mod reads the
projectile's position every frame and adds it to the line, until the projectile stops moving.

Darts and bullets are told apart by their average speed over the first tenth of a second: between 10 and 150 m/s
it is a dart.

## Troubleshooting

- **The window says "Not active: the game's projectile code is not where it was expected"**: the game has been
  updated and the addresses the mod uses have moved. The mod turns itself off instead of patching the wrong code;
  it needs an update.
- **"Darts seen" goes up but no line appears**: check that **Enabled** is ticked and the line color is not
  transparent.
- The SDK log is `HITMAN 3/Retail/ZHMModLoader.log`.

## Building from source

The mod is built inside the ZHMModSDK source tree:

1. Clone [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) and follow its build instructions.
2. Copy this repository's `CMakeLists.txt` and `Src/` into `Mods/BulletTracer/` of the SDK.
3. Add `BulletTracer` to the `set(MODS ...)` list in the SDK's root `CMakeLists.txt`.
4. Build the `BulletTracer` target (`cmake --preset x64-Release`, then
   `cmake --build _build/x64-Release --target BulletTracer`).

## Credits

- [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) by Orfeas Zafeiris. The infinite ammo follows the way the SDK's
  Player mod does it.
- Made by Fra47. The code was written with Claude Code ( Opus 5.5), with the game's internals
  studied in Ghidra and checked in game.

## License

GPL-3.0, the same license as ZHMModSDK. See `LICENSE`.
