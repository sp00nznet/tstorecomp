# Triage: `libscorpio.so`

- machine: `EM_AARCH64`
- file: 28.2 MB, `.text`: 19.8 MB @ `0x6e5000`
- instructions: 4,940,609 (707 distinct mnemonics)
- functions from `.eh_frame`: **62,008** covering 98.8% of `.text`
- undisassembled bytes: 24,916

## Shim surface

Linked libraries -- every one of these is a shim you write:

- `libc++_shared.so`
- `libjnigraphics.so`
- `libz.so`
- `liblog.so`
- `libGLESv1_CM.so`
- `libGLESv2.so`
- `libandroid.so`
- `libopenal.so`
- `libEGL.so`
- `libNimble.so`
- `libc.so`
- `libm.so`
- `libdl.so`

Undefined symbols to satisfy: **481**  
Exported symbols: **12653**

JNI entry points (the Java glue you must replace): **73**

```
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESDestroy
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESInit
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRender
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRenderGLLoadingScreen
Java_com_bight_android_jni_BGCoreJNIBridge_OGLESResize
Java_com_bight_android_jni_BGCoreJNIBridge_destroy
Java_com_bight_android_jni_BGCoreJNIBridge_init
Java_com_bight_android_jni_BGCoreJNIBridge_keyPressed
Java_com_bight_android_jni_BGCoreJNIBridge_keyReleased
Java_com_bight_android_jni_BGCoreJNIBridge_pause
Java_com_bight_android_jni_BGCoreJNIBridge_pointerMoved
Java_com_bight_android_jni_BGCoreJNIBridge_pointerPressed
Java_com_bight_android_jni_BGCoreJNIBridge_pointerReleased
Java_com_bight_android_jni_BGCoreJNIBridge_resume
Java_com_ea_nimble_bridge_BaseNativeCallback_nativeCallback
Java_com_ea_nimble_bridge_BaseNativeCallback_nativeFinalize
Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_onApplicationLaunch
Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_onApplicationQuit
Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_onApplicationResume
Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_onApplicationSuspend
Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_onUpdateLaunchMethod
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_cleanup
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_restore
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_resume
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_setup
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_suspend
Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_00024NimbleCppComponent_teardown
Java_com_ea_simpsons_AppCenterJava_setupNativeCrashesListener
Java_com_ea_simpsons_BackgroundDownloaderJava_downloadComplete
Java_com_ea_simpsons_BackgroundDownloaderJava_obfuscateFileName
Java_com_ea_simpsons_ScorpioJNI_DisplayAirplaneModeError
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerLoginComplete
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerLogoutComplete
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerPopulateFriendDetailsAddFriend
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerPopulateFriendDetailsComplete
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerPopulateUserDetailsComplete
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerReauthorizeDataAccessComplete
Java_com_ea_simpsons_ScorpioJNI_FacebookManagerSendRequestComplete
Java_com_ea_simpsons_ScorpioJNI_GetNativeUserAge
Java_com_ea_simpsons_ScorpioJNI_GetNativeUserID
...
```

## Constructs the lifter must special-case

| construct | count |
|---|---|
| indirect-branch | 103,969 |
| exclusive | 11,512 |
| lse-atomic | 42 |
| barrier | 109 |
| syscall | 115 |
| sysreg | 1,571 |
| pointer-auth | 166 |

## Top mnemonics

| mnemonic | count |
|---|---|
| `mov` | 984,008 |
| `ldr` | 683,818 |
| `add` | 417,574 |
| `bl` | 363,719 |
| `str` | 250,271 |
| `cmp` | 201,893 |
| `ldp` | 179,088 |
| `b` | 156,345 |
| `stp` | 155,015 |
| `adrp` | 148,523 |
| `cbz` | 142,565 |
| `blr` | 99,004 |
| `sub` | 94,238 |
| `b.eq` | 69,222 |
| `fadd` | 63,223 |
| `ret` | 55,938 |
| `strb` | 52,109 |
| `scvtf` | 51,633 |
| `ldrb` | 46,173 |
| `cbnz` | 43,919 |
| `b.ne` | 43,356 |
| `fmov` | 41,682 |
| `fmul` | 39,552 |
| `tbz` | 35,138 |
| `ldur` | 28,273 |
