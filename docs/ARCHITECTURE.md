# Architecture

## The shape of the target

`libscorpio.so` is a stock NDK r23c C++ build: stripped, position-independent,
ARM64, with complete `.eh_frame` unwind tables. It exports 12,653 symbols and
imports 481 (776 once the libraries the APK ships beside it are counted too). Of the 73 `Java_*` JNI exports, exactly one cluster matters:

```
Java_com_bight_android_jni_BGCoreJNIBridge_init
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESInit
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESResize
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRender
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRenderGLLoadingScreen
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESDestroy
Java_com_bight_android_jni_BGCoreJNIBridge_pointerPressed
Java_com_bight_android_jni_BGCoreJNIBridge_pointerMoved
Java_com_bight_android_jni_BGCoreJNIBridge_pointerReleased
Java_com_bight_android_jni_BGCoreJNIBridge_keyPressed
Java_com_bight_android_jni_BGCoreJNIBridge_keyReleased
Java_com_bight_android_jni_BGCoreJNIBridge_pause
Java_com_bight_android_jni_BGCoreJNIBridge_resume
Java_com_bight_android_jni_BGCoreJNIBridge_destroy
```

That is a complete game host contract: lifecycle, a GL surface, a resize hook, a
render tick, and pointer/key input. A desktop host implements those fourteen
calls and nothing else. The remaining JNI exports are platform services —
Facebook login, push notifications, IAP, crash reporting, background downloads —
which are stubbed or wired to the self-hosted server.

The rest of the Java side (Firebase, Google Play Billing, the multidex loader)
has no counterpart on desktop and is discarded rather than translated. The two
`classes*.dex` files are never lifted.

## Two execution paths, one host

The host program is required either way, so it is built first.

**Path A — native ARM64 host.** On Apple Silicon, Windows-on-ARM and ARM64
Linux the instructions in `libscorpio.so` already run. What it lacks is Bionic
and Android: a loader that maps the ELF and resolves its imports against the
shim is enough to call `init` / `OGLESRender` directly. This gets a real,
playable window early and validates the shim, the asset paths, the GL usage and
the server wiring before any lifting exists to be blamed for a bug.

**Path B — lifted x86-64.** The same host, linked against generated C instead of
a mapped ELF. Path A is the reference implementation Path B is diffed against;
when a lifted function misbehaves, the ARM64 build is the oracle.

## The loader (M1, done)

`host/elf_image.cpp`, no dependencies. The image turned out to be about as
simple as an NDK shared object gets, which is why this is ~350 lines rather
than a vendored ELF library:

- **Three `PT_LOAD` segments** — one r-x of 26.8 MB, two rw- totalling 810 KB.
- **98,172 relocations in four types**: 80,248 `R_AARCH64_RELATIVE`, 11,122
  `ABS64`, 6,068 `JUMP_SLOT`, 734 `GLOB_DAT`. Nothing else appears, and an
  unrecognised type is a hard error rather than a skipped entry.
- **No `PT_TLS`**, no TLSDESC. Thread-local storage would have meant modelling
  a TLS block per guest thread; the engine reaches `TPIDR_EL0` directly instead,
  which is one of the things the lifter's 1,571 `mrs` sites cover.
- **1,527 static constructors** in `DT_INIT_ARRAY`, which run before `init`.
- `DT_SYMTAB` carries no length, so the symbol count (13,135) comes from the
  SysV hash table's `nchain` field.

Unresolved imports bind into a `PROT_NONE` guard page, one 8-byte slot each, so
a call into a shim that does not exist yet faults at an address that identifies
the missing symbol. A null binding would fault too, and tell you nothing.

`tools/selftest.py` hand-assembles a synthetic aarch64 `.so` exporting the same
fourteen contract symbols and covering all four relocation types, so the loader
is verifiable on any machine without the game present.

## Import surface, measured

`tsto_host` groups unresolved imports by provider. The counts are in the README;
the two findings that shape M2 are absences:

- **No `egl*` imports.** `libEGL.so` is in `DT_NEEDED` but contributes nothing.
  Context and surface creation live on the Java side, so on desktop the host's
  windowing library does it and there is no EGL shim.
- **No `libNimble.so` imports.** Nimble is reached only through the
  `Java_com_ea_nimble_bridge_*` exports, which are called *into* the engine from
  Java. The engine never calls out to it, so it needs no shim either — only a
  decision never to make those calls.

## The shim (M2, in progress)

`host/shim.cpp` answers an import in three layers, in order: explicit
implementations, name aliases, then the host C runtime looked up by name at load
time. The third layer is why 245 libc and 15 libm imports cost almost no code —
ordinary standard C is already in the host's CRT, so binding it by name is free.

The alias layer has one rule worth stating: **an alias must be ABI-identical,
not merely similar.** Bionic's `mkdir(path, mode)` and the Microsoft CRT's
`_mkdir(path)` take different arguments. Aliasing them would compile, link, run,
and corrupt the stack. Anything whose signature differs is deliberately left
unresolved so it appears in the work list and gets a real wrapper.

Above the shim sits a simpler rule: **if the APK ships it, load it.** Every
`DT_NEEDED` entry present next to the engine is loaded as its own image and used
to satisfy the engine's imports; everything else is an Android system library
and falls through to the shim. That is what answers libc++_shared's 114
NDK-mangled (`_ZNSt6__ndk1...`) symbols, which no host STL can provide.

The exception proves the rule's worth. Loading the shipped `libopenal.so`
resolves 32 symbols but introduces six `libOpenSLES.so` imports, because its
audio backend is Android's. We only learned that by loading it. Native
openal-soft has the same API over WASAPI/CoreAudio/ALSA, so it gets linked
instead.

Current state: **776 imports across four images, 381 resolved, 292 unique
outstanding.** The remainder, grouped by what the work actually is:

| Owed by | Left | Desktop replacement |
|---|---:|---|
| `libc.so` | 225 | 36 `pthread_*` over Win32/pthreads, POSIX file I/O and `mmap`, `dirent`, time. Bionic-only entry points (the FORTIFY `_chk` family, `__errno`, `__assert2`, `__stack_chk_*`) are already forwarded |
| `libGLESv2.so`, `libGLESv1_CM.so` | 50 | ANGLE, or desktop GL directly — most ES2 entry points are name-identical. GLESv1_CM means a fixed-function path is still live and needs auditing |
| `libOpenSLES.so` | 6 | none — dropped along with the shipped `libopenal.so`, replaced by native openal-soft |
| `libdl.so` | 5 | `dlopen`/`dlsym`/`dlclose`/`dlerror` over the loaded image set. `dl_iterate_phdr` is how the C++ unwinder finds `.eh_frame`, so exceptions need it to report our mapped segments |
| `libjnigraphics.so` | 3 | bitmap lock/unlock around a raw pixel buffer |
| `libc++_shared.so` | 1 | the APK's own copy, loaded as an image (2,437 symbols) |
| `libm.so` | 1 | host libm |
| `libz.so` | 0 | linked zlib |
| `liblog.so` | 0 | forwarded to `stderr` |
| `libEGL.so`, `libNimble.so` | 0 | nothing imported; neither needs a shim |

## The lifter (M3)

Per-function ARM64 → C, in the ps3recomp mould.

**Boundaries** come from `.eh_frame` — 62,008 FDEs covering 98.8% of `.text`.
The remaining 1.2% is jump tables, PLT-ish thunks and alignment padding, found by
sweeping the gaps.

**Indirect branches** are the bulk of the difficulty: 99,004 `blr` plus 4,965
other indirect forms, nearly all C++ virtual dispatch. Resolution is a sorted
address → function-pointer table over the 62,008 known starts, with a trap on
miss so an unlifted target is a loud failure rather than a silent one.

**Exclusives** (11,512 `ldxr`/`stxr` pairs, 42 LSE atomics) lower to
`std::atomic` compare-exchange loops. The pairs must be recognised as pairs; a
standalone `stxr` is a lifting bug, not a translation unit.

**Everything else is mechanical.** The top of the mnemonic histogram is `mov`,
`ldr`, `add`, `bl`, `str`, `cmp`, `ldp` — ordinary register and memory work,
plus a large NEON float block (`fadd`, `fmul`, `scvtf`) that maps onto SSE/AVX
intrinsics.

**Hand-audited tails.** 115 `svc` sites, 1,571 `mrs`/`msr` (mostly `TPIDR_EL0`
thread-local reads), 166 pointer-authentication instructions, and 24,916 bytes
capstone could not decode. Small enough to review individually.

## Server and content

The client fetches DLC and talks gameplay protocol over plain HTTP to a base
URL. In a native build that URL is a configuration value rather than a byte
patch, which also removes the "clean APK only" constraint the Android patcher
has.

The server runs **inside the app as a loopback sidecar** — started on
`127.0.0.1` at launch, shut down on exit. No container, no LAN, nothing leaving
the machine. A sidecar process rather than a linked-in library because it reuses
the existing protocol implementation unchanged, and because upstream
`d-fens/tsto_server` states no licence, so its code can be referenced as a
submodule but never vendored into an MIT repository or linked into an MIT
binary. A separate process is not a derivative work.

`server/` is that submodule, pointing at our fork. The town modifiers live
there and attach over the same loopback. After M3 the netcode is ours, so the
HTTP hop could collapse into direct in-process calls — worth doing only if it
ever measurably matters.
