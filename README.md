# tstorecomp

> *The Simpsons: Tapped Out* as a native cross-platform desktop application —
> with the server and the town modifiers built into the app, not bolted on
> beside it.

**Status: M2 in progress.** The window opens, the engine loads, and 613 of 776
imports resolve. See [Milestones](#milestones).

---

## What this is

TSTO is not a Java game. Everything that matters — the renderer, the
simulation, the isometric town — lives in a single 28 MB ARM64 shared object
(`libscorpio.so`, Bight Games' *Scorpio* engine). The Java side is a 14-method
JNI bridge that owns a window, a GL context, and a touch handler. Nothing else.

So this is a port, not an emulator: replace the Android host with a native
desktop host, satisfy the library's small POSIX/OpenGL import surface, and lift
the ARM64 code to C for machines that are not ARM.

It is also more than a port. EA's servers went dark in January 2025, so the
client needs one regardless; folding a local server and the town-editing tools
*into the binary* is the difference between a technical exercise and something
worth running. The goal is a single desktop app that boots into your Springfield
with the modifiers already there.

## This repo is the thin half

The loader, the Bionic/Android shim layer, the host window and the triage tools
are not specific to this game, and they live in
**[androidrecomp](https://github.com/sp00nznet/androidrecomp)**, vendored here as
a submodule. That separation was made early on purpose: extracting a reusable
kit *after* a port has grown into it is painful and rarely happens.

What remains here is what is genuinely about this title:

```
tstorecomp/
├── androidrecomp/          # submodule -- loader, shims, window, tools
├── server/                 # submodule -- self-hosted server + town modifiers
├── contract/bgcore.txt     # the 14 JNI entry points the host drives
├── docs/
│   ├── ARCHITECTURE.md
│   └── triage-libscorpio-arm64.md
└── CMakeLists.txt
```

At the moment that is one text file — which is exactly the point. The JNI
bridge, the asset layout and the server integration land here next.

## Legal / content policy

Tools only. No game code, no game assets, no extracted sprites, no save data, no
EA binaries — `.gitignore` blocks all of it, deliberately. You supply your own
legally obtained APK; everything here operates on a file you already have.
Licensed MIT; contributions must be your own work.

## Building

```sh
git clone --recursive https://github.com/sp00nznet/tstorecomp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/tsto_host --contract=contract/bgcore.txt path/to/libscorpio.so
./build/tsto_host --window --contract=contract/bgcore.txt path/to/libscorpio.so
```

On Windows add `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
so CMake finds zlib and SDL2. A toolchain file only takes effect on a fresh
cache, so delete `build/` if you add it later.

Point it at the `lib/arm64-v8a/` directory of an extracted APK — the loader uses
the libraries sitting next to the engine to satisfy its imports.

## What the target looks like

Measured against a stock 4.69.x `arm64-v8a` build
([full triage report](docs/triage-libscorpio-arm64.md)):

| | |
|---|---|
| `.text` | 19.8 MB, 4,940,609 instructions, 707 distinct mnemonics |
| Functions recovered from `.eh_frame` | **62,008**, covering **98.8%** of `.text` |
| Relocations | 98,172, in only 4 types, and **no TLS segment** |
| Static constructors | 1,527 |
| JNI entry points | 73 (the whole game loop is 14 of them) |
| Indirect branches (`br`/`blr`) | 103,969 |
| Exclusives / LSE atomics | 11,512 / 42 |
| `svc` / `mrs`,`msr` | 115 / 1,571 |

Near-total `.eh_frame` coverage removes the single hardest problem in static
recompilation — function boundary recovery is guesswork on a stripped console
binary, and here it is read straight out of the unwind tables. The 104k indirect
branches are C++ vtable dispatch and are the real work, but with 62k known
function starts they reduce to an address → function-pointer table.

## Import surface

Across the engine and the three libraries the APK ships beside it: **776
imports, 613 resolved, 130 unique outstanding.**

| Provider | Left | Notes |
|---|---:|---|
| `libc.so` | 113 | file I/O, `mmap`, sockets, signals, process |
| `libOpenSLES.so` | 6 | goes away with the shipped `libopenal.so`, replaced by native openal-soft |
| `libdl.so` | 5 | `dl_iterate_phdr` is how the C++ unwinder finds `.eh_frame`, so exceptions depend on it |
| `libjnigraphics.so` | 3 | bitmap lock/unlock around a raw pixel buffer |
| `libc++_shared.so` | 1 | `__sF` — needs Bionic's `sizeof(FILE)`, deliberately not guessed |
| `libm.so`, misc | 2 | |
| `libGLESv2/v1_CM` | **0** | desktop GL exports all 50 under identical names — zero wrappers |
| `libz.so`, `liblog.so` | **0** | linked zlib; log forwarded to stderr |

Three findings shaped this. `libEGL.so` and `libNimble.so` sit in `DT_NEEDED`
yet import **zero** symbols — EGL context creation happened on the Java side, and
Nimble (EA's identity/telemetry/IAP SDK) is only ever called *into* from Java.
Neither needs a shim, and the host's windowing library makes the GL context
anyway.

libc++ solved itself: its 114 NDK-mangled (`_ZNSt6__ndk1...`) symbols cannot come
from any host STL, but the APK ships `libc++_shared.so`, so the loader loads it.

And GL cost nothing. A desktop GL 2.1 compatibility context exports every GLES2
and GLESv1_CM entry point the engine imports, under identical names, so all 50
bind straight through the driver.

## Milestones

- [x] **M0 — Triage.** Measure the target.
- [x] **M1 — Loader.** Map, relocate, bind, protect; verify the host contract.
- [ ] **M2 — Shim + window.** In progress: **613 of 776 resolved, 130 unique
      outstanding**, and an SDL2 window with a live GL context. Left: file I/O
      and `mmap`, sockets, audio, and the JNI bridge that lets the host actually
      call the fourteen entry points.
- [ ] **M3 — Lifter.** ARM64 → C over the 62,008 recovered functions, with an
      arm64 build as the oracle to diff against.
- [ ] **M4 — x86-64.** Windows first, Linux and macOS from the same C.

On an arm64 host — Apple Silicon, Windows-on-ARM, arm64 Linux — M2 is the last
milestone before the game runs: the engine's instructions execute natively, so
finishing the shim and the bridge is enough, with no lifting involved.

## Server and town modifiers

The app should not need a container, a LAN, or anything outside the process. The
game speaks plain HTTP to a configurable base URL, so the server runs **inside
the app as a loopback sidecar** — started on `127.0.0.1` at launch, shut down on
exit. Nothing leaves the machine.

A sidecar process rather than a linked-in library, for two reasons: it reuses
~2,000 lines of working protocol code with no rewrite, and it keeps a clean
licence boundary. Upstream
[`d-fens/tsto_server`](https://github.com/d-fens/tsto_server) states no licence
at all, so its code cannot be vendored into an MIT repository or linked into an
MIT binary. A submodule references it without redistributing it, and a separate
process is not a derivative work.

`server/` points at [`sp00nznet/tsto-springfield`](https://github.com/sp00nznet/tsto-springfield),
our fork with the boot-loop and persistence fixes. It is private for now and
will need to be public before this repository is.

On Android the server URL has to be patched into `libscorpio.so` by hash and
offset. In a native build it is just a config value — which also removes the
"clean APK only" constraint the Android patcher has.

## Credits

*The Simpsons: Tapped Out* is © Electronic Arts. This project is not affiliated
with or endorsed by EA, Bight Games, or Fox. It ships no EA code or content.
