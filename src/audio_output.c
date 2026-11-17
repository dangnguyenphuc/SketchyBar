#include "audio_output.h"
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 120000
#define kAudioObjectPropertyElementMain kAudioObjectPropertyElementMaster
#endif

static const AudioObjectPropertyAddress g_default_device_address = {
  kAudioHardwarePropertyDefaultOutputDevice,
  kAudioObjectPropertyScopeGlobal,
  kAudioObjectPropertyElementMain
};

// Volume lives either on the main element or per channel (1 = left,
// 2 = right), depending on the device.
static const AudioObjectPropertyElement g_volume_elements[] = {
  kAudioObjectPropertyElementMain, 1, 2
};
#define VOLUME_ELEMENT_COUNT 3

static struct audio_output g_state;
static audio_output_callback* g_callback = NULL;
static AudioObjectID g_device = kAudioObjectUnknown;

static AudioObjectPropertyAddress volume_address(AudioObjectPropertyElement e) {
  return (AudioObjectPropertyAddress){ kAudioDevicePropertyVolumeScalar,
                                       kAudioObjectPropertyScopeOutput,
                                       e                                };
}

static AudioObjectPropertyAddress mute_address() {
  return (AudioObjectPropertyAddress){ kAudioDevicePropertyMute,
                                       kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain };
}

static AudioObjectID default_device() {
  AudioObjectID device = kAudioObjectUnknown;
  uint32_t size = sizeof(device);
  AudioObjectGetPropertyData(kAudioObjectSystemObject,
                             &g_default_device_address,
                             0, NULL, &size, &device   );
  return device;
}

static bool settable(AudioObjectID device,
                     const AudioObjectPropertyAddress* address) {
  Boolean is_settable = false;
  return AudioObjectHasProperty(device, address)
         && AudioObjectIsPropertySettable(device,
                                          address,
                                          &is_settable) == noErr
         && is_settable;
}

// The device's volume: the main element if it has one, otherwise the average
// of the channels that do.
static bool read_volume(AudioObjectID device, float* volume) {
  float sum = 0;
  int count = 0;
  for (int i = 0; i < VOLUME_ELEMENT_COUNT; i++) {
    AudioObjectPropertyAddress address = volume_address(g_volume_elements[i]);
    if (!AudioObjectHasProperty(device, &address)) continue;

    float value = 0;
    uint32_t size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &value)
        != noErr) {
      continue;
    }
    if (i == 0) {
      *volume = value;
      return true;
    }
    sum += value;
    count++;
  }
  if (count == 0) return false;
  *volume = sum / count;
  return true;
}

static void read_state(struct audio_output* state) {
  memset(state, 0, sizeof(*state));
  if (g_device == kAudioObjectUnknown) return;

  state->has_volume = read_volume(g_device, &state->volume);

  AudioObjectPropertyAddress mute = mute_address();
  if (AudioObjectHasProperty(g_device, &mute)) {
    uint32_t muted = 0;
    uint32_t size = sizeof(muted);
    if (AudioObjectGetPropertyData(g_device, &mute, 0, NULL, &size, &muted)
        == noErr) {
      state->has_mute = true;
      state->muted = muted != 0;
    }
  }

  AudioObjectPropertyAddress name_address = { kAudioObjectPropertyName,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMain };
  CFStringRef name = NULL;
  uint32_t size = sizeof(name);
  if (AudioObjectGetPropertyData(g_device, &name_address, 0, NULL, &size, &name)
        == noErr && name) {
    CFStringGetCString(name,
                       state->name,
                       sizeof(state->name),
                       kCFStringEncodingUTF8);
    CFRelease(name);
  }
}

// Main thread.
static void update(void* context) {
  if (!g_callback) return;

  struct audio_output state;
  read_state(&state);
  if (memcmp(&state, &g_state, sizeof(state)) == 0) return;
  g_state = state;
  g_callback();
}

// CoreAudio calls listeners on its own thread; hop to the main thread.
static OSStatus property_listener(AudioObjectID object,
                                  uint32_t count,
                                  const AudioObjectPropertyAddress* addresses,
                                  void* context                              ) {
  dispatch_async_f(dispatch_get_main_queue(), NULL, update);
  return noErr;
}

static void watch_device(AudioObjectID device, bool watch) {
  if (device == kAudioObjectUnknown) return;

  AudioObjectPropertyAddress addresses[VOLUME_ELEMENT_COUNT + 1];
  for (int i = 0; i < VOLUME_ELEMENT_COUNT; i++)
    addresses[i] = volume_address(g_volume_elements[i]);
  addresses[VOLUME_ELEMENT_COUNT] = mute_address();

  for (int i = 0; i < VOLUME_ELEMENT_COUNT + 1; i++) {
    if (!AudioObjectHasProperty(device, &addresses[i])) continue;
    if (watch) {
      AudioObjectAddPropertyListener(device, &addresses[i],
                                     property_listener, NULL);
    } else {
      AudioObjectRemovePropertyListener(device, &addresses[i],
                                        property_listener, NULL);
    }
  }
}

static void device_changed(void* context) {
  if (!g_callback) return;
  watch_device(g_device, false);
  g_device = default_device();
  watch_device(g_device, true);
  update(NULL);
}

static OSStatus device_listener(AudioObjectID object,
                                uint32_t count,
                                const AudioObjectPropertyAddress* addresses,
                                void* context                              ) {
  dispatch_async_f(dispatch_get_main_queue(), NULL, device_changed);
  return noErr;
}

void audio_output_start(audio_output_callback* on_change) {
  if (g_callback) return;
  g_callback = on_change;

  AudioObjectAddPropertyListener(kAudioObjectSystemObject,
                                 &g_default_device_address,
                                 device_listener, NULL     );
  g_device = default_device();
  watch_device(g_device, true);
  read_state(&g_state);
}

void audio_output_stop(void) {
  if (!g_callback) return;

  AudioObjectRemovePropertyListener(kAudioObjectSystemObject,
                                    &g_default_device_address,
                                    device_listener, NULL     );
  watch_device(g_device, false);
  g_device = kAudioObjectUnknown;
  g_callback = NULL;
  memset(&g_state, 0, sizeof(g_state));
}

const struct audio_output* audio_output_get(void) {
  return &g_state;
}

void audio_output_set_volume(float volume) {
  if (g_device == kAudioObjectUnknown) return;
  volume = max(min(volume, 1.f), 0.f);

  // Main element if settable, otherwise every channel that is.
  AudioObjectPropertyAddress main = volume_address(g_volume_elements[0]);
  if (settable(g_device, &main)) {
    AudioObjectSetPropertyData(g_device, &main, 0, NULL,
                               sizeof(volume), &volume  );
  } else {
    for (int i = 1; i < VOLUME_ELEMENT_COUNT; i++) {
      AudioObjectPropertyAddress address = volume_address(g_volume_elements[i]);
      if (settable(g_device, &address)) {
        AudioObjectSetPropertyData(g_device, &address, 0, NULL,
                                   sizeof(volume), &volume     );
      }
    }
  }

  // Like the volume keys: turning it up unmutes.
  if (volume > 0 && g_state.muted) audio_output_set_muted(false);
  update(NULL);
}

void audio_output_set_muted(bool muted) {
  AudioObjectPropertyAddress address = mute_address();
  if (g_device == kAudioObjectUnknown || !settable(g_device, &address)) return;

  uint32_t value = muted ? 1 : 0;
  AudioObjectSetPropertyData(g_device, &address, 0, NULL,
                             sizeof(value), &value       );
  update(NULL);
}
