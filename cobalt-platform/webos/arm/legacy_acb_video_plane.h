#ifndef STARBOARD_WEBOS_ARM_LEGACY_ACB_VIDEO_PLANE_H_
#define STARBOARD_WEBOS_ARM_LEGACY_ACB_VIDEO_PLANE_H_

#include <dlfcn.h>
#include <cstdio>
#include <mutex>

namespace starboard {
namespace shared {
namespace webos {

// webOS 2-4 video-plane adapter. This implementation targets legacy LG webOS 4.
// Resolve the TV's existing libAcbAPI at runtime: no SDK ACB stubs or bundled
// firmware libraries are required. No process-wide signal handlers installed.
class LegacyAcbVideoPlane {
 public:
  LegacyAcbVideoPlane() = default;
  ~LegacyAcbVideoPlane() { Close(); }
  LegacyAcbVideoPlane(const LegacyAcbVideoPlane&) = delete;
  LegacyAcbVideoPlane& operator=(const LegacyAcbVideoPlane&) = delete;

  bool Open(const char* app_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (id_ || !app_id || !*app_id) return false;
    library_ = dlopen("libAcbAPI.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library_) {
      std::fprintf(stderr, "[webOS4 ACB] dlopen failed: %s\n", dlerror());
      return false;
    }
    if (!Resolve(create_, "AcbAPI_create") ||
        !Resolve(initialize_, "AcbAPI_initialize") ||
        !Resolve(set_media_id_, "AcbAPI_setMediaId") ||
        !Resolve(set_sink_type_, "AcbAPI_setSinkType") ||
        !Resolve(set_state_, "AcbAPI_setState") ||
        !Resolve(set_display_window_, "AcbAPI_setDisplayWindow") ||
        !Resolve(set_media_video_data_, "AcbAPI_setMediaVideoData") ||
        !Resolve(finalize_, "AcbAPI_finalize") ||
        !Resolve(destroy_, "AcbAPI_destroy")) {
      std::fprintf(stderr, "[webOS4 ACB] required API symbol missing\n");
      CloseLocked();
      return false;
    }
    id_ = create_();
    if (!id_ || !initialize_(id_, 10 /* PLAYER_TYPE_MSE */, app_id,
                              &LegacyAcbVideoPlane::Callback)) {
      std::fprintf(stderr, "[webOS4 ACB] create/initialize failed\n");
      CloseLocked();
      return false;
    }
    initialized_ = true;
    std::fprintf(stderr, "[webOS4 ACB] initialized app=%s\n", app_id);
    return true;
  }

  bool BindMediaId(const char* media_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!initialized_ || !media_id || !*media_id) return false;
    const bool ok = set_media_id_(id_, media_id);
    std::fprintf(stderr, "[webOS4 ACB] bind media ID %s\n",
                 ok ? "accepted" : "rejected");
    return ok;
  }

  // Call *after* Starfish Load() returns true, not after LOADCOMPLETED.
  void AfterLoad(int width, int height) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!initialized_ || width <= 0 || height <= 0) return;
    set_display_window_(id_, 0, 0, width, height, true, &task_id_);
  }

  // webOS 4: attach the sink only after LOADCOMPLETED.
  void OnLoadCompleted() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!initialized_ || loaded_) return;
    set_sink_type_(id_, 0 /* SINK_TYPE_MAIN */);
    set_state_(id_, 1 /* FOREGROUND */, 1 /* LOADED */, &task_id_);
    loaded_ = true;
    if (play_pending_) {
      play_pending_ = false;
      set_state_(id_, 1, 2 /* PLAYING */, &task_id_);
    }
  }

  // First accepted Feed can precede LOADCOMPLETED. Queue PLAYING until LOADED.
  void OnFirstAcceptedBuffer() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!initialized_) return;
    if (!loaded_) {
      play_pending_ = true;
    } else {
      set_state_(id_, 1, 2 /* PLAYING */, &task_id_);
    }
  }

  // Re-enter PLAYING after a successful Starfish Play(), including resume.
  void OnPlaying() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!initialized_) return;
    if (!loaded_) {
      play_pending_ = true;
    } else {
      set_state_(id_, 1, 2 /* PLAYING */, &task_id_);
    }
  }

  // The SDK header for setMediaVideoData has historically been wrong;
  // the TV's C wrapper accepts a third task-ID pointer.
  void OnVideoInfo(const char* json) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_ && json && *json) {
      set_media_video_data_(id_, json, &task_id_);
    }
  }

  void OnPause() {
    std::lock_guard<std::mutex> guard(mutex_);
    play_pending_ = false;
    if (initialized_ && loaded_) {
      set_state_(id_, 1, 3 /* PAUSED */, &task_id_);
    }
  }

  void OnUnload() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initialized_ && loaded_) {
      set_state_(id_, 1, 0 /* UNLOADED */, &task_id_);
    }
    loaded_ = false;
    play_pending_ = false;
  }

  void Close() {
    std::lock_guard<std::mutex> guard(mutex_);
    CloseLocked();
  }

 private:
  using CallbackFn = void (*)(long, long, long, long, long, const char*);
  using CreateFn = long (*)();
  using InitializeFn = bool (*)(long, int, const char*, CallbackFn);
  using SetMediaIdFn = bool (*)(long, const char*);
  using SetSinkTypeFn = bool (*)(long, int);
  using SetStateFn = int (*)(long, int, int, long*);
  using SetDisplayWindowFn = int (*)(long, long, long, long, long, bool, long*);
  using SetMediaVideoDataFn = int (*)(long, const char*, long*);
  using FinalizeFn = int (*)(long);
  using DestroyFn = void (*)(long);

  template <typename Fn>
  bool Resolve(Fn& output, const char* symbol) {
    output = reinterpret_cast<Fn>(dlsym(library_, symbol));
    return output != nullptr;
  }
  void CloseLocked() {
    if (id_ && initialized_) {
      if (loaded_) set_state_(id_, 1, 0, &task_id_);
      finalize_(id_);
    }
    if (id_ && destroy_) destroy_(id_);
    id_ = 0;
    initialized_ = false;
    loaded_ = false;
    play_pending_ = false;
    if (library_) dlclose(library_);
    library_ = nullptr;
  }
  static void Callback(long id, long task, long event, long app,
                       long play, const char* reply) {
    std::fprintf(stderr,
                 "[webOS4 ACB] callback id=%ld task=%ld event=%ld app=%ld play=%ld %s\n",
                 id, task, event, app, play, reply ? reply : "");
  }

  std::mutex mutex_;
  void* library_ = nullptr;
  long id_ = 0;
  long task_id_ = 0;
  bool initialized_ = false;
  bool loaded_ = false;
  bool play_pending_ = false;
  CreateFn create_ = nullptr;
  InitializeFn initialize_ = nullptr;
  SetMediaIdFn set_media_id_ = nullptr;
  SetSinkTypeFn set_sink_type_ = nullptr;
  SetStateFn set_state_ = nullptr;
  SetDisplayWindowFn set_display_window_ = nullptr;
  SetMediaVideoDataFn set_media_video_data_ = nullptr;
  FinalizeFn finalize_ = nullptr;
  DestroyFn destroy_ = nullptr;
};

}  // namespace webos
}  // namespace shared
}  // namespace starboard
#endif  // STARBOARD_WEBOS_ARM_LEGACY_ACB_VIDEO_PLANE_H_
