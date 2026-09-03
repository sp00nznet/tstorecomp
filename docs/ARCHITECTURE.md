# Architecture

## The shape of the target

`libscorpio.so` is a stock NDK r23c C++ build: stripped, position-independent,
ARM64, with complete `.eh_frame` unwind tables. It exports 12,653 symbols and
imports 481. Of the 73 `Java_*` JNI exports, exactly one cluster matters:

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
and Android: a loader that maps the ELF and resolves its 481 imports against the
shim is enough to call `init` / `OGLESRender` directly. This gets a real,
playable window early and validates the shim, the asset paths, the GL usage and
the server wiring before any lifting exists to be blamed for a bug.

**Path B — lifted x86-64.** The same host, linked against generated C instead of
a mapped ELF. Path A is the reference implementation Path B is diffed against;
when a lifted function misbehaves, the ARM64 build is the oracle.

## The shim (M2)

481 undefined symbols across 13 libraries. Grouped by what the work actually is:

| Android library | Desktop replacement | Notes |
|---|---|---|
| `libc.so`, `libm.so`, `libdl.so` | host libc | Bionic-specific entry points (`__system_property_get`, `__cxa_atexit` variants) need thin wrappers |
| `libc++_shared.so` | libc++ / system STL | ABI-compatible enough on the lifted path; Path A maps the shipped copy |
| `libz.so` | zlib | direct |
| `libEGL.so`, `libGLESv2.so`, `libGLESv1_CM.so` | ANGLE | GLESv1_CM means a fixed-function path is still in use; ANGLE covers ES2, the ES1 calls need auditing |
| `libopenal.so` | openal-soft | the game already ships openal-soft; same API |
| `liblog.so` | printf | trivial |
| `libandroid.so` | file I/O | `AAssetManager` over the extracted `assets/` tree |
| `libjnigraphics.so` | stb_image | bitmap lock/unlock around a raw pixel buffer |
| `libNimble.so` | stub | EA's service SDK (identity, telemetry, IAP). Its transport is the same HTTP the self-hosted server answers; the SDK itself is stubbed to success |

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
URL. A self-hosted server answers it. In a native build the URL is a
configuration value rather than a byte patch, which also removes the
"clean APK only" constraint the Android patcher has.

Town-modifier tooling attaches to that server, not to this repository — see the
README's policy section.
