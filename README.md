# tstorecomp

> Static recompilation toolkit for turning *The Simpsons: Tapped Out*'s native
> Android engine into a real cross-platform desktop application — with the
> server and the town modifiers built into the app, not bolted on beside it.

**Status: M2 in progress.** The loader runs and 563 of 776 imports resolve.
See [Milestones](#milestones).

---

## What this is

TSTO is not a Java game. Everything that matters — the renderer, the
simulation, the isometric town — lives in a single 28 MB ARM64 shared object
(`libscorpio.so`, Bight Games' *Scorpio* engine). The Java side is a 14-method
JNI bridge that owns a window, a GL context, and a touch handler. Nothing else.

That makes it a good static-recompilation target: replace the Android host with
a native desktop host, satisfy the library's small POSIX/OpenGL import surface,
and lift the ARM64 code to C for non-ARM machines. The output is an ordinary
native executable — no emulator, no Android runtime, no APK.

It is also more than a recompile. EA's servers are gone, so the client needs one
regardless; folding a local server and the town-editing tools *into the binary*
is the difference between a port and something worth running. The goal is a
single desktop app that boots into your Springfield with the modifiers already
there.

Same philosophy as [N64Recomp](https://github.com/N64Recomp/N64Recomp),
[UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) and our own
[ps3recomp](https://github.com/sp00nznet/ps3recomp), aimed at an Android title
instead of a console one.

## Legal / content policy

This repository contains **tools only**. No game code, no game assets, no
extracted sprites, no save data, no EA binaries — `.gitignore` blocks all of it
and that is deliberate, not incidental. You supply your own legally obtained
APK. Everything here operates on a file you already have.

Licensed MIT. Contributions must be your own work; do not paste in code from
projects whose licensing is unclear.

## Building

Needs CMake 3.20+ and any C++17 compiler. zlib is optional; without it those
12 imports stay on the work list.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

python tools/selftest.py          # loader self-check, no APK required
./build/tsto_host path/to/libscorpio.so
```

On Windows, add `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
so CMake finds zlib. A toolchain file only takes effect on a fresh cache, so
delete `build/` if you add it later.

`tsto_host` maps the engine, applies its relocations, binds its imports, and
prints what the shim still owes it. `selftest.py` builds a synthetic aarch64
`.so` that exports the same fourteen host-contract symbols and exercises all
four relocation types, so the loader is testable on any machine with no game
content present.

The Python tools need `capstone` and `pyelftools`:

```sh
pip install capstone pyelftools
python tools/apk_probe.py game.apk --out docs/triage.md
```

## What the target looks like

Measured against a stock 4.69.x `arm64-v8a` build
([full triage report](docs/triage-libscorpio-arm64.md)):

| | |
|---|---|
| `.text` | 19.8 MB, 4,940,609 instructions, 707 distinct mnemonics |
| Functions recovered from `.eh_frame` | **62,008**, covering **98.8%** of `.text` |
| Relocations | 98,172, in only 4 types, and **no TLS segment** |
| Static constructors | 1,527 |
| Undefined symbols to satisfy | **481** |
| JNI entry points | 73 (the whole game loop is 14 of them) |
| Indirect branches (`br`/`blr`) | 103,969 |
| Exclusives / LSE atomics | 11,512 / 42 |
| `svc` / `mrs`,`msr` | 115 / 1,571 |

Near-total `.eh_frame` coverage removes the single hardest problem in static
recompilation — function boundary recovery is guesswork on a stripped console
binary, and here it is read straight out of the unwind tables. Four relocation
types and no thread-local storage make the loader small. The 104k indirect
branches are C++ vtable dispatch and are the real work, but with 62k known
function starts they reduce to an address → function-pointer table rather than
open-ended analysis.

### The shim surface

`tsto_host` loads the engine plus every library the APK ships beside it, then
groups what nothing provides by the library that owes it. Across all four
images: **776 imports, 563 resolved, 180 unique still outstanding.**

| Provider | Left | How the rest got resolved / what remains |
|---|---:|---|
| `libc.so` | 113 | file I/O, `mmap`, sockets, signals, process. Threads, locale, time and BSD string helpers are done; ordinary standard C binds to the host CRT by name |
| `libGLESv2.so`, `libGLESv1_CM.so` | 50 | needs a GL context first. GLESv1_CM means a fixed-function path is still live |
| `libOpenSLES.so` | 6 | see below |
| `libdl.so` | 5 | `dlopen`/`dlsym`/`dlclose`/`dlerror`/`dl_iterate_phdr` — the last is how the C++ unwinder finds `.eh_frame`, so exceptions depend on it |
| `libjnigraphics.so` | 3 | bitmap lock/unlock around a raw pixel buffer |
| `libc++_shared.so` | 1 | **satisfied by loading the APK's own copy** — 2,437 symbols |
| `libm.so` | 1 | host libm |
| `libz.so` | 0 | **linked zlib** — Bionic ships stock zlib, every signature matches |
| `liblog.so` | 0 | forwarded to `stderr` |

Three findings shaped this. `libEGL.so` and `libNimble.so` are in `DT_NEEDED`
yet import **zero** symbols — EGL context creation happens on the Java side, and
Nimble (EA's identity/telemetry/IAP SDK) is only ever called *into* from Java.
Neither needs a shim; the host's windowing library makes the GL context anyway.

And the libc++ problem solved itself. Its 114 imports are NDK-mangled
(`_ZNSt6__ndk1...`) libc++ internals that no host STL can provide — but the APK
ships `libc++_shared.so`, so the loader just loads it. Anything in `DT_NEEDED`
that the APK ships is loaded and used; everything else is an Android system
library and falls to the shim.

That rule has one exception, and loading it is how we found out: the shipped
`libopenal.so` resolves 32 symbols but drags in six `libOpenSLES.so` imports,
because its audio backend is Android's. Native openal-soft has WASAPI,
CoreAudio and ALSA backends and the same API, so it gets linked instead.

## Architecture

```
tstorecomp/
├── host/
│   ├── elf_image.{h,cpp}   # aarch64 ELF loader: map, relocate, bind, protect
│   ├── shim.{h,cpp}        # imports: explicit impls, aliases, host CRT by name
│   ├── shim_pthread.cpp    # threads, semaphores, TLS keys
│   ├── shim_posix.cpp      # locale, time, stdio, BSD string, wide char
│   └── main.cpp            # tsto_host: load and report the shim work list
├── tools/
│   ├── apk_probe.py        # feasibility triage: imports, functions, ISA histogram
│   └── selftest.py         # synthetic engine + loader assertions
├── server/                 # submodule -> sp00nznet/tsto-springfield
├── docs/
│   ├── ARCHITECTURE.md     # the plan, in detail
│   └── triage-*.md         # generated reports
└── lifter/                 # (M3) aarch64 -> C
```

## Milestones

- [x] **M0 — Triage.** Measure the target.
- [x] **M1 — Loader.** Map the engine's three segments, apply all 98,172
      relocations, bind its 481 imports, locate the fourteen host-contract entry
      points, and set page protections. Unresolved imports bind into a guard
      page one slot apiece, so calling a missing shim faults at an address that
      names the symbol instead of dereferencing null. Needed by both execution
      paths, so it is built first and runs on any host.
- [ ] **M2 — Shim + window.** In progress: **563 of 776 imports resolved, 180
      unique outstanding.** Done — APK-shipped dependencies are loaded and used,
      ordinary standard C binds to the host CRT by name, zlib is linked, and
      Bionic's FORTIFY, compiler-support, threading, locale, time, stdio and BSD
      string entry points are implemented. Left: file I/O and `mmap`, sockets,
      signals, the 50 GL calls, OpenAL, and a desktop window with a GL context
      and input driving the fourteen entry points. On an arm64 host that last
      step is a playable game, with no lifting involved.
- [ ] **M3 — Lifter.** ARM64 → C over the 62,008 recovered functions, with the
      arm64 build as the oracle to diff against.
- [ ] **M4 — x86-64.** Windows first, Linux and macOS from the same C.

## Server and town modifiers

The app should not need a container, a LAN, or anything outside the process.
The game speaks plain HTTP to a configurable base URL, so the plan is a
**loopback sidecar**: the desktop app starts the server on `127.0.0.1` at
launch and shuts it down on exit. Nothing leaves the machine.

A sidecar rather than a linked-in library, for two reasons. It reuses ~2,000
lines of already-working protocol code with no rewrite, and it keeps a clean
licence boundary — upstream [`d-fens/tsto_server`](https://github.com/d-fens/tsto_server)
states no licence at all, so its code cannot be vendored into an MIT repository
or linked into an MIT binary. A submodule references it without redistributing
it, and a separate process is not a derivative work.

`server/` is therefore a submodule pointing at
[`sp00nznet/tsto-springfield`](https://github.com/sp00nznet/tsto-springfield) —
our fork, which carries the boot-loop and persistence fixes. It is private for
now and will need to be public before this repository is.

On Android the server URL has to be patched into `libscorpio.so` by hash and
offset. In a native build it is just a config value, which also removes the
"clean APK only" constraint the Android patcher has.

The town modifiers — protobuf save editing, the isometric viewer, the sprite
pipeline — attach over that same loopback HTTP and stay in the submodule. Once
the netcode is ours after M3, the HTTP hop can collapse into direct in-process
calls if it ever proves worth it.

## Credits

*The Simpsons: Tapped Out* is © Electronic Arts. This project is not affiliated
with or endorsed by EA, Bight Games, or Fox. It ships no EA code or content.
