# tstorecomp

> Static recompilation toolkit for turning *The Simpsons: Tapped Out*'s native
> Android engine into a real cross-platform desktop application.

**Status: triage complete, nothing runs yet.** See
[Milestones](#milestones) for what is actually done.

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

Same philosophy as [N64Recomp](https://github.com/N64Recomp/N64Recomp),
[UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) and our own
[ps3recomp](https://github.com/sp00nznet/ps3recomp), aimed at an Android title
instead of a console one.

### Why bother, when the game already ran on a phone

- **A real desktop client.** Mouse, keyboard, a window you can resize, a town
  you can see at 4K instead of on a 6-inch screen.
- **Moddability.** Lifted C is hackable in ways an ARM64 blob is not — the
  town-modifier work stops being a web view rendered *next to* the game and
  becomes the game itself.
- **Preservation.** EA shut the servers down in January 2025. A native binary
  outlives both the phone and the emulator that ran it.
- **Portability.** The output is C, so it targets whatever a compiler targets.

## Legal / content policy

This repository contains **tools only**. No game code, no game assets, no
extracted sprites, no save data, no EA binaries — `.gitignore` blocks all of it
and that is deliberate, not incidental. You supply your own legally obtained
APK. Everything here operates on a file you already have.

Licensed MIT. Contributions must be your own work; do not paste in code from
projects whose licensing is unclear.

## Triage results

Run against a stock 4.69.x `arm64-v8a` build
([full report](docs/triage-libscorpio-arm64.md)):

| | |
|---|---|
| `.text` | 19.8 MB, 4,940,609 instructions, 707 distinct mnemonics |
| Functions recovered from `.eh_frame` | **62,008**, covering **98.8%** of `.text` |
| Undefined symbols to satisfy | **481** |
| Linked libraries | 13, all standard (libc/libm/libz/EGL/GLESv1_CM/GLESv2/OpenAL/…) |
| JNI entry points | 73 (the whole game loop is 14 of them) |
| Indirect branches (`br`/`blr`) | 103,969 |
| Exclusives / LSE atomics | 11,512 / 42 |
| `svc` / `mrs`,`msr` | 115 / 1,571 |

**What those numbers mean.** Near-total `.eh_frame` coverage removes the single
hardest problem in static recompilation — function boundary recovery is
guesswork on a stripped console binary, and here it is simply read out of the
unwind tables. A 481-symbol import surface is small and entirely conventional.
The 104k indirect branches are C++ vtable dispatch and are the real work, but
with 62k known function starts they reduce to an address → function-pointer
table rather than open-ended analysis.

## Architecture

```
tstorecomp/
├── tools/
│   └── apk_probe.py      # feasibility triage: imports, functions, ISA histogram
├── docs/
│   ├── ARCHITECTURE.md   # the plan, in detail
│   └── triage-*.md       # generated reports
├── host/                 # (M1) native window, GL context, input -> JNI bridge
├── shim/                 # (M2) the 481 imports: libc, GLES/EGL, OpenAL, assets
└── lifter/               # (M3) aarch64 -> C
```

## Usage

```sh
pip install capstone pyelftools

# Triage any Android game's native engine -- APK or extracted directory.
python tools/apk_probe.py game.apk
python tools/apk_probe.py extracted/ --abi arm64-v8a --out docs/triage.md
```

## Milestones

- [x] **M0 — Triage.** Measure the target. Done; numbers above.
- [ ] **M1 — Native host.** A desktop window, a GL context, and mouse/keyboard
      input driving the 14 `BGCoreJNIBridge` entry points. On an ARM64 host the
      stock `.so` can be loaded directly, so this milestone produces a running
      game *before* a single instruction is lifted — and the same host is what
      the recompiled build uses later.
- [ ] **M2 — Shim layer.** The 481 undefined symbols. Mostly libc/libm/libz
      (system), GLESv2/EGL (ANGLE), OpenAL (openal-soft — the game already
      ships it), `liblog` (printf), `libandroid` (asset manager → plain files),
      `libjnigraphics` (bitmap lock → stb), `libNimble` (EA's service SDK;
      stubbed, see below).
- [ ] **M3 — Lifter.** ARM64 → C over the 62,008 recovered functions.
- [ ] **M4 — x86-64.** Windows first, Linux and macOS from the same C.

## Server and town modifier

The game speaks plain HTTP to a configurable base URL, so a self-hosted server
stands in for EA's. On Android that URL has to be patched into `libscorpio.so`
by hash and offset; in a native build it is just a config value — strictly
better, and the seam the existing town-modifier tooling already plugs into.

That tooling (protobuf save editing, the isometric viewer, the sprite pipeline)
is **not vendored here**. It derives from an upstream project with no stated
licence, which cannot be relicensed MIT, and it attaches over HTTP anyway —
nothing about it needs to live in this repo. It stays where it is and points at
the native client exactly as it points at the Android one.

## Credits

*The Simpsons: Tapped Out* is © Electronic Arts. This project is not affiliated
with or endorsed by EA, Bight Games, or Fox. It ships no EA code or content.
