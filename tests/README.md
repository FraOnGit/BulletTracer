# Regression tests

From the repository root, with Python 3 and a GCC/Clang-compatible C++17 compiler:

```sh
python3 tests/run_tests.py
```

For Clang with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
CXX=clang++ CXXFLAGS='-fsanitize=address,undefined -g' python3 tests/run_tests.py
```

The runner extracts the current `Trace` definition, constants, and affected method bodies from `Src/` into
a temporary C++ harness. The production logic is not copied into the tests. Engine entities, position reads,
and frame-callback removal are test doubles because the SDK and Windows game cannot run on every test host.
Generated files and binaries are removed after the run. Compilation errors or failed checks return nonzero.

Coverage includes ordinary darts/bullets, short impacts, projectile invalidation, address reuse during play
and pause, stationary shots, and deactivation/deletion order on unload. These checks cannot validate vtable
addresses, the Windows SEH position reader, engine threading, or the game's entity/ability implementation.

Before release, build the DLL against ZHMModSDK 4.2.1 on Windows and check in HITMAN 3.290.1:

1. Enable infinite ammo, then disable BulletTracer through the SDK Mods list. Ammo should return to normal
   when no other mod or game mechanic provides infinite ammo. Re-enable BulletTracer and repeat.
2. Repeat with infinite ammo already off, then with a mission restart and a level change.
3. Fire darts at nearby and distant surfaces. In default darts-only mode, measured short flights should remain
   visible. A shot that ends before two different positions are sampled still has insufficient speed data.
4. Fire successive shots while earlier traces are still active. Previous lines must not jump to the next shot.
5. Pause/resume through FreeCam during flight. The trace should resume without a false landing or extra segment.
6. Check normal bullets with Trace all shots both off and on, and check both depth-rendering modes.
