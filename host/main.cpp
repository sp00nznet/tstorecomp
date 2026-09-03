// tsto_host -- loads the game engine and reports what the shim still owes it.
//
// On an arm64 host the loaded image is callable and this is the beginning of
// the real host process. Everywhere else it is a load-and-verify pass: the
// segment layout, relocations and symbol binding it performs are exactly what
// the lifter consumes, so they are exercised on whatever machine you have.

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "elf_image.h"

namespace {

// The Android host contract. Fourteen calls: lifecycle, a GL surface, and
// input. A desktop host implements these and nothing else.
const char* const kEntryPoints[] = {
    "Java_com_bight_android_jni_BGCoreJNIBridge_init",
    "Java_com_bight_android_jni_BGCoreJNIBridge_OGLESInit",
    "Java_com_bight_android_jni_BGCoreJNIBridge_OGLESResize",
    "Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRender",
    "Java_com_bight_android_jni_BGCoreJNIBridge_OGLESRenderGLLoadingScreen",
    "Java_com_bight_android_jni_BGCoreJNIBridge_OGLESDestroy",
    "Java_com_bight_android_jni_BGCoreJNIBridge_pointerPressed",
    "Java_com_bight_android_jni_BGCoreJNIBridge_pointerMoved",
    "Java_com_bight_android_jni_BGCoreJNIBridge_pointerReleased",
    "Java_com_bight_android_jni_BGCoreJNIBridge_keyPressed",
    "Java_com_bight_android_jni_BGCoreJNIBridge_keyReleased",
    "Java_com_bight_android_jni_BGCoreJNIBridge_pause",
    "Java_com_bight_android_jni_BGCoreJNIBridge_resume",
    "Java_com_bight_android_jni_BGCoreJNIBridge_destroy",
};

// Bionic only version-tags libc/libm/libdl, so two thirds of the imports
// arrive unversioned in one heap. Name prefixes say which shim owes them,
// which is the question the M2 work list actually needs answered.
std::string Provider(const tsto::Import& i) {
  if (!i.version.empty()) return i.version;
  const std::string& n = i.name;
  auto starts = [&n](const char* p) { return n.rfind(p, 0) == 0; };
  if (starts("_Z") || starts("__cxa") || starts("__gxx")) return "libc++_shared.so";
  if (starts("gl")) return "libGLESv2.so / libGLESv1_CM.so";
  if (starts("egl")) return "libEGL.so";
  if (starts("alc") || starts("al")) return "libopenal.so";
  if (starts("Nimble") || starts("nimble")) return "libNimble.so";
  if (starts("AAsset") || starts("ANative") || starts("AConfiguration") ||
      starts("AInput") || starts("ALooper"))
    return "libandroid.so";
  if (starts("AndroidBitmap")) return "libjnigraphics.so";
  if (starts("__android_log")) return "liblog.so";
  if (starts("inflate") || starts("deflate") || starts("crc32") ||
      starts("compress") || starts("uncompress") || starts("gz") ||
      starts("zlib") || starts("adler32"))
    return "libz.so";
  return "(unclassified)";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <libscorpio.so>\n", argv[0]);
    return 2;
  }

  tsto::ElfImage image;
  std::string err;
  // No shim implementations yet, so nothing resolves and every import binds to
  // its own trap slot. Swap in the shim lookup here as M2 lands.
  if (!image.Load(argv[1], nullptr, &err)) {
    fprintf(stderr, "load failed: %s\n", err.c_str());
    return 1;
  }

  printf("image      %s\n", argv[1]);
  printf("base       %p, span %.1f MB\n", static_cast<void*>(image.base()),
         image.span() / 1e6);
  printf("segments   %zu\n", image.segments().size());
  for (const tsto::Segment& s : image.segments()) {
    printf("           %#010llx +%#010llx  %c%c%c\n",
           static_cast<unsigned long long>(s.vaddr),
           static_cast<unsigned long long>(s.memsz),
           (s.flags & 4) ? 'r' : '-', (s.flags & 2) ? 'w' : '-',
           (s.flags & 1) ? 'x' : '-');
  }
  printf("symbols    %zu\n", image.symbol_count());
  printf("relocs     %zu applied\n", image.relocations_applied());
  printf("initarray  %zu constructors\n", image.init_array().size());

  printf("\nneeded (%zu)\n", image.needed().size());
  for (const std::string& n : image.needed()) printf("  %s\n", n.c_str());

  // The M2 work list: every unresolved import, grouped by the library that is
  // supposed to provide it.
  std::map<std::string, std::vector<const tsto::Import*>> by_provider;
  size_t resolved = 0;
  for (const tsto::Import& i : image.imports()) {
    if (i.resolved) {
      ++resolved;
      continue;
    }
    by_provider[Provider(i)].push_back(&i);
  }
  printf("\nimports    %zu total, %zu resolved, %zu outstanding\n",
         image.imports().size(), resolved, image.imports().size() - resolved);
  for (const auto& kv : by_provider) {
    printf("\n  %s -- %zu\n", kv.first.c_str(), kv.second.size());
    for (const tsto::Import* i : kv.second) printf("    %s\n", i->name.c_str());
  }

  printf("\nhost contract\n");
  int missing = 0;
  for (const char* name : kEntryPoints) {
    uint64_t addr = image.Lookup(name);
    if (!addr) ++missing;
    const char* leaf = strrchr(name, '_');
    printf("  %-28s %s\n", leaf ? leaf + 1 : name,
           addr ? "found" : "MISSING");
  }
  if (missing) {
    fprintf(stderr, "\n%d host entry point(s) missing -- wrong library?\n",
            missing);
    return 1;
  }

  if (!image.Protect(&err)) {
    fprintf(stderr, "protect failed: %s\n", err.c_str());
    return 1;
  }

#if defined(__aarch64__) || defined(_M_ARM64)
  printf("\narm64 host: image is executable. Running it needs the M2 shim.\n");
#else
  printf("\nx86-64 host: image loaded and relocated but not executable here;\n"
         "             running it needs the M3 lifter.\n");
#endif
  return 0;
}
