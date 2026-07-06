/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// WASAPI (Windows Audio Session API) implementation of IAudioInput.
// Selected via make_audio_input() at the bottom of this TU. Plugin-only —
// the CMake build gates this file behind `if(WIN32)`. It is the Windows
// counterpart to audio_input_coreaudio.cpp (macOS AUHAL) and
// audio_input_pulseaudio.cpp (Linux pa_simple).
//
// A background thread owns the whole capture chain: it initialises COM,
// opens the default capture endpoint in shared mode, and drains
// IAudioCaptureClient in an event-driven loop. Unlike CoreAudio (which
// resamples in the HAL) and PulseAudio (server-side resample), raw WASAPI
// hands us the device mix format — typically 32-bit float, 44.1/48 kHz,
// stereo — so we downmix to mono and streaming-resample to 16 kHz int16
// ourselves before appending to the capture buffer. The buffer therefore
// holds the same 16 kHz mono 16-bit PCM as the other two backends, and
// sample_rate_hz() reports 16000.

#include "audio/i_audio_input.hpp"
#include "audio/mic_permission.hpp"
#include "persistence/settings.hpp"

#include <XPLMUtilities.h>

#define WIN32_LEAN_AND_MEAN
// These Win32 audio headers have a strict include order: <windows.h>
// first, and <ksmedia.h> (KSDATAFORMAT_SUBTYPE_*) depends on
// WAVEFORMATEXTENSIBLE from <mmreg.h>.
// clang-format off
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
// clang-format on

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace audio {

namespace {

constexpr unsigned kSampleRate = 16000;

// Streaming linear resampler + downmixer. Consumes device-native mono
// float samples one at a time and emits 16 kHz int16 samples, carrying
// the fractional read position across GetBuffer() chunk boundaries so
// there are no seams. Reset at the start of each recording session.
class Resampler {
public:
  void reset(double src_rate_hz) {
    step_ = src_rate_hz / static_cast<double>(kSampleRate);
    next_out_pos_ = 0.0;
    src_index_ = 0.0;
    prev_ = 0.0f;
  }

  // Feed one source sample; append any produced output samples to `out`.
  void feed(float s, std::vector<int16_t> &out) {
    // Emit every output whose source-space position falls at or before
    // the current source index. The first sample (src_index_ == 0) is
    // emitted verbatim (frac == 1 -> value == s).
    while (next_out_pos_ <= src_index_) {
      double frac = next_out_pos_ - (src_index_ - 1.0);
      float val = prev_ + (s - prev_) * static_cast<float>(frac);
      out.push_back(to_int16(val));
      next_out_pos_ += step_;
    }
    prev_ = s;
    src_index_ += 1.0;
  }

private:
  static int16_t to_int16(float v) {
    if (v > 1.0f)
      v = 1.0f;
    else if (v < -1.0f)
      v = -1.0f;
    return static_cast<int16_t>(std::lround(v * 32767.0f));
  }

  double step_ = 1.0;       // source samples per output sample
  double next_out_pos_ = 0; // source-space position of next output
  double src_index_ = 0;    // running source sample index
  float prev_ = 0.0f;       // previous source sample (for interpolation)
};

// COM smart-release helper — RAII for the various WASAPI interfaces.
template <class T> struct ComPtr {
  T *p = nullptr;
  ~ComPtr() {
    if (p)
      p->Release();
  }
  T **operator&() { return &p; }
  T *operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

void log(const char *msg) { XPLMDebugString(msg); }

class WasapiInput : public IAudioInput {
public:
  WasapiInput() = default;
  ~WasapiInput() override { close(); }

  bool open() override;
  void close() override;

  void start_recording() override;
  void stop_recording() override;

  std::vector<int16_t> take_pcm() override;
  unsigned sample_rate_hz() const override { return kSampleRate; }
  std::size_t buffer_samples() const override;
  float duration_seconds() const override;

private:
  void capture_loop();
  // Owns every COM interface for one session; returns before the caller
  // calls CoUninitialize so all interfaces are released while COM is
  // still initialised on this thread.
  void run_capture_session();
  bool decode_format(const WAVEFORMATEX *wf);
  float read_sample(const BYTE *frame, unsigned channel) const;

  std::thread capture_thread_;
  std::atomic<bool> capture_running_{false};
  std::atomic<bool> recording_{false};

  // Set by the capture thread once init has succeeded or failed, so
  // open() can report the real result to the caller.
  std::atomic<bool> init_done_{false};
  std::atomic<bool> init_ok_{false};

  // Device mix-format description resolved at init time.
  unsigned src_rate_hz_ = 0;
  unsigned channels_ = 0;
  unsigned bytes_per_sample_ = 0; // per single channel
  bool is_float_ = false;         // else integer PCM

  Resampler resampler_;
  std::vector<int16_t> buffer_;
  mutable std::mutex buffer_mutex_;
};

bool WasapiInput::decode_format(const WAVEFORMATEX *wf) {
  channels_ = wf->nChannels;
  src_rate_hz_ = wf->nSamplesPerSec;
  bytes_per_sample_ = wf->wBitsPerSample / 8u;

  if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    is_float_ = true;
  } else if (wf->wFormatTag == WAVE_FORMAT_PCM) {
    is_float_ = false;
  } else if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wf);
    if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
      is_float_ = true;
    else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
      is_float_ = false;
    else
      return false;
  } else {
    return false;
  }

  // We handle float32 and 16-/32-bit integer PCM. Anything else is
  // rejected so decoding never reads a format it can't interpret.
  if (is_float_ && bytes_per_sample_ != 4)
    return false;
  if (!is_float_ && bytes_per_sample_ != 2 && bytes_per_sample_ != 4)
    return false;
  return channels_ > 0;
}

float WasapiInput::read_sample(const BYTE *frame, unsigned channel) const {
  const BYTE *p = frame + static_cast<size_t>(channel) * bytes_per_sample_;
  if (is_float_) {
    float v;
    std::memcpy(&v, p, sizeof(v));
    return v;
  }
  if (bytes_per_sample_ == 2) {
    int16_t v;
    std::memcpy(&v, p, sizeof(v));
    return static_cast<float>(v) / 32768.0f;
  }
  // 32-bit integer PCM.
  int32_t v;
  std::memcpy(&v, p, sizeof(v));
  return static_cast<float>(v) / 2147483648.0f;
}

void WasapiInput::capture_loop() {
  // COM must be initialised on the thread that uses the WASAPI
  // interfaces. Multithreaded apartment matches our detached worker
  // model. run_capture_session() owns every COM interface and releases
  // them all before returning, so CoUninitialize here runs only after
  // the last Release — never the other way round.
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool com_inited = SUCCEEDED(hr);
  run_capture_session();
  if (com_inited)
    CoUninitialize();
}

void WasapiInput::run_capture_session() {
  auto fail_init = [&]() {
    init_ok_ = false;
    init_done_ = true;
  };

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&enumerator));
  if (FAILED(hr) || !enumerator) {
    log("[xp_wellys_atc] WASAPI: device enumerator creation failed\n");
    return fail_init();
  }

  ComPtr<IMMDevice> device;
  hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
  if (FAILED(hr) || !device) {
    log("[xp_wellys_atc] WASAPI: no default capture endpoint\n");
    return fail_init();
  }

  ComPtr<IAudioClient> client;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void **>(&client));
  if (FAILED(hr) || !client) {
    log("[xp_wellys_atc] WASAPI: IAudioClient activation failed\n");
    return fail_init();
  }

  WAVEFORMATEX *mix = nullptr;
  hr = client->GetMixFormat(&mix);
  if (FAILED(hr) || !mix) {
    log("[xp_wellys_atc] WASAPI: GetMixFormat failed\n");
    return fail_init();
  }
  bool fmt_ok = decode_format(mix);

  // Event-driven shared-mode capture. 200 ms buffer is plenty; the
  // event fires each time the device has a period of audio ready.
  HANDLE audio_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  REFERENCE_TIME buffer_duration = 2000000; // 200 ms in 100-ns units
  if (fmt_ok) {
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, buffer_duration,
                            0, mix, nullptr);
  }
  if (!fmt_ok || FAILED(hr) || !audio_event) {
    log("[xp_wellys_atc] WASAPI: unsupported mix format or Initialize "
        "failed\n");
    if (mix)
      CoTaskMemFree(mix);
    if (audio_event)
      CloseHandle(audio_event);
    return fail_init();
  }
  CoTaskMemFree(mix);
  mix = nullptr;

  hr = client->SetEventHandle(audio_event);
  ComPtr<IAudioCaptureClient> capture;
  if (SUCCEEDED(hr))
    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void **>(&capture));
  if (FAILED(hr) || !capture) {
    log("[xp_wellys_atc] WASAPI: capture service unavailable\n");
    CloseHandle(audio_event);
    return fail_init();
  }

  hr = client->Start();
  if (FAILED(hr)) {
    log("[xp_wellys_atc] WASAPI: stream Start failed\n");
    CloseHandle(audio_event);
    return fail_init();
  }

  {
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "[xp_wellys_atc] Audio recorder initialized (WASAPI %u Hz "
                  "%u ch %s -> 16kHz mono 16-bit)\n",
                  src_rate_hz_, channels_, is_float_ ? "float" : "int");
    log(msg);
  }

  init_ok_ = true;
  init_done_ = true;

  std::vector<int16_t> scratch;
  while (capture_running_.load()) {
    // Wait for the next audio period; wake periodically to re-check the
    // running flag so close() returns promptly.
    WaitForSingleObject(audio_event, 100);

    UINT32 packet = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
      BYTE *data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
        break;

      if (recording_.load() && frames > 0) {
        scratch.clear();
        const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        const size_t frame_bytes =
            static_cast<size_t>(channels_) * bytes_per_sample_;
        for (UINT32 i = 0; i < frames; ++i) {
          float mono = 0.0f;
          if (!silent && data) {
            const BYTE *frame = data + static_cast<size_t>(i) * frame_bytes;
            for (unsigned c = 0; c < channels_; ++c)
              mono += read_sample(frame, c);
            mono /= static_cast<float>(channels_);
          }
          resampler_.feed(mono, scratch);
        }
        if (!scratch.empty()) {
          std::lock_guard<std::mutex> lock(buffer_mutex_);
          buffer_.insert(buffer_.end(), scratch.begin(), scratch.end());
        }
      }

      capture->ReleaseBuffer(frames);
    }
  }

  client->Stop();
  CloseHandle(audio_event);
  // All ComPtr locals Release here as they leave scope, before
  // capture_loop() calls CoUninitialize.
}

bool WasapiInput::open() {
  mic_permission::check_and_request(); // no-op on Windows, kept for symmetry

  init_done_ = false;
  init_ok_ = false;
  capture_running_ = true;
  capture_thread_ = std::thread(&WasapiInput::capture_loop, this);

  // Block until the capture thread reports init success or failure so
  // open() returns a truthful result (matches the synchronous open()
  // contract of the CoreAudio / PulseAudio backends).
  while (!init_done_.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

  if (!init_ok_.load()) {
    close();
    return false;
  }
  return true;
}

void WasapiInput::close() {
  capture_running_ = false;
  if (capture_thread_.joinable())
    capture_thread_.join();
  recording_ = false;
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  buffer_.clear();
}

void WasapiInput::start_recording() {
  if (!capture_running_.load()) {
    log("[xp_wellys_atc] Warning: audio recorder not initialized\n");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
  }
  resampler_.reset(src_rate_hz_);
  recording_ = true;
}

void WasapiInput::stop_recording() {
  recording_ = false;
  if (settings::debug_logging()) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    int16_t peak = 0;
    for (auto s : buffer_) {
      int16_t abs_s = s < 0 ? static_cast<int16_t>(-s) : s;
      if (abs_s > peak)
        peak = abs_s;
    }
    float peak_pct = (static_cast<float>(peak) / 32767.0f) * 100.0f;
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "[xp_wellys_atc][DEBUG] Recording stopped: %zu samples, "
                  "peak: %d (%.1f%%)\n",
                  buffer_.size(), static_cast<int>(peak), peak_pct);
    log(msg);
  }
}

std::vector<int16_t> WasapiInput::take_pcm() {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  return std::move(buffer_);
}

std::size_t WasapiInput::buffer_samples() const {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  return buffer_.size();
}

float WasapiInput::duration_seconds() const {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  if (buffer_.empty())
    return 0.0f;
  return static_cast<float>(buffer_.size()) / static_cast<float>(kSampleRate);
}

} // namespace

std::unique_ptr<IAudioInput> make_audio_input() {
  return std::make_unique<WasapiInput>();
}

} // namespace audio
