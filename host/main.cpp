// tsto_host -- loads the game engine and reports what the shim still owes it.
//
// On an arm64 host the loaded image is callable and this is the beginning of
// the real host process. Everywhere else it is a load-and-verify pass: the
// segment layout, relocations and symbol binding it performs are exactly what
// the lifter consumes, so they are exercised on whatever machine you have.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "elf_image.h"
#include "shim.h"

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
// which is the question the work list actually needs answered.
std::string Provider(const tsto::Import& i) {
  if (!i.version.empty()) return i.version;
  const std::string& n = i.name;
  auto starts = [&n](const char* p) { return n.rfind(p, 0) == 0; };
  if (starts("_Z") || starts("__cxa") || starts("__gxx")) return "libc++_shared.so";
  if (starts("gl")) return "libGLESv2.so / libGLESv1_CM.so";
  if (starts("egl")) return "libEGL.so";
  if (starts("SL") || starts("sl")) return "libOpenSLES.so";
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

  const std::filesystem::path path(argv[1]);
  const std::filesystem::path dir = path.parent_path();
  std::string err;

  // Libraries the APK ships alongside the engine are loaded and used to
  // satisfy its imports. Everything else in DT_NEEDED is an Android system
  // library and falls to the shim. This is what answers libc++_shared's 114
  // NDK-mangled symbols, which no host STL can provide.
  std::vector<std::unique_ptr<tsto::ElfImage>> deps;
  auto resolve = [&deps](const char* name) -> uint64_t {
    for (const auto& d : deps)
      if (uint64_t a = d->Lookup(name)) return a;
    return tsto::ShimResolve(name);
  };

  printf("dependencies\n");
  for (const std::string& name : tsto::ElfImage::ReadNeeded(path.string())) {
    const std::filesystem::path p = dir / name;
    if (!std::filesystem::exists(p)) {
      printf("  %-22s system -- shimmed\n", name.c_str());
      continue;
    }
    auto img = std::make_unique<tsto::ElfImage>();
    if (!img->Load(p.string(), resolve, &err)) {
      printf("  %-22s FAILED: %s\n", name.c_str(), err.c_str());
      continue;
    }
    printf("  %-22s loaded -- %zu symbols, %zu relocs\n", name.c_str(),
           img->symbol_count(), img->relocations_applied());
    deps.push_back(std::move(img));
  }

  tsto::ElfImage image;
  if (!image.Load(path.string(), resolve, &err)) {
    fprintf(stderr, "load failed: %s\n", err.c_str());
    return 1;
  }

  printf("\nimage      %s\n", path.filename().string().c_str());
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

  // The work list, across every image loaded: what nothing yet provides,
  // grouped by the library that owes it.
  std::map<std::string, std::vector<std::string>> outstanding;
  size_t total = 0, resolved = 0;
  auto tally = [&](const tsto::ElfImage& img) {
    for (const tsto::Import& i : img.imports()) {
      ++total;
      if (i.resolved) {
        ++resolved;
        continue;
      }
      auto& bucket = outstanding[Provider(i)];
      if (std::find(bucket.begin(), bucket.end(), i.name) == bucket.end())
        bucket.push_back(i.name);
    }
  };
  for (const auto& d : deps) tally(*d);
  tally(image);

  size_t unique_outstanding = 0;
  for (const auto& kv : outstanding) unique_outstanding += kv.second.size();
  printf("\nimports    %zu total, %zu resolved, %zu outstanding (%zu unique)\n",
         total, resolved, total - resolved, unique_outstanding);
  for (const auto& kv : outstanding) {
    printf("\n  %s -- %zu\n", kv.first.c_str(), kv.second.size());
    for (const std::string& n : kv.second) printf("    %s\n", n.c_str());
  }

  printf("\nhost contract\n");
  int missing = 0;
  for (const char* name : kEntryPoints) {
    uint64_t addr = image.Lookup(name);
    if (!addr) ++missing;
    const char* leaf = strrchr(name, '_');
    printf("  %-28s %s\n", leaf ? leaf + 1 : name, addr ? "found" : "MISSING");
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
  printf("\narm64 host: image is executable once the work list is empty.\n");
#else
  printf("\nx86-64 host: image loaded and relocated but not executable here;\n"
         "             running it needs the M3 lifter.\n");
#endif
  return 0;
}
