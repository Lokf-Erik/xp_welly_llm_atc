# xp_wellys_atc — Windows Port

Windows 10 / 11 support for [xp_wellys_atc](README.md).

> **Status**: cloud-only port; build enabled via GitHub Actions
> (`windows-latest`, native MSVC). Runtime verification on X-Plane 12
> Windows in progress.
> Branch: `windows_support`.

---

## Supported platforms

| OS | Arch | Backends available |
|---|---|---|
| Windows 10 (x64) | x86_64 (`win_x64`) | **OpenAI Cloud**, **Mistral Cloud** |
| Windows 11 (x64) | x86_64 (`win_x64`) | same |

> **Cloud-only.** The Windows slice does **not** compile the local
> whisper.cpp / llama.cpp / Piper backends (no Metal, no onnxruntime).
> STT → LM → TTS runs entirely over the OpenAI or Mistral cloud APIs —
> functionally identical to the x86_64 (Intel) macOS slice. Local CPU
> inference (already available on Linux) is a later stage.

---

## Runtime requirements

| Item | Requirement |
|---|---|
| X-Plane | 12 (x64, tested 12.1+) |
| Network | Internet connection — all inference is cloud-based |
| API key | OpenAI **or** Mistral key (bring your own) |
| Microphone | Any capture device; Windows mic privacy must allow desktop apps (see below) |
| Extra DLLs | **None** — libcurl is linked statically (Schannel TLS) |
| Disk | ~a few MB — no local models are downloaded in cloud mode |

---

## Install (pre-built binary)

The Windows build is produced by CI (there is no local MSVC toolchain on
the developer's Mac). Two ways to obtain it:

**A. Download the CI artifact from a dev box** (macOS/Linux with `gh`):
```bash
make ci-remote      # push the branch + trigger the Windows build
# …wait for the run to go green…
make win-artifact   # downloads the drop-in folder into dist-win/
```
`make win-artifact` fetches the `xp_wellys_atc-win` artifact into
`dist-win/xp_wellys_atc/`.

**B. Download from the GitHub Actions run** directly: open the workflow
run in the browser and download the **`xp_wellys_atc-win`** artifact, or
grab the combined release ZIP from a tagged release (it carries the
`win_x64/` slice alongside `mac_x64/` and `lin_x64/`).

Then copy the `xp_wellys_atc/` folder into your X-Plane plugins directory:
```
X-Plane 12\Resources\plugins\
```

Plugin directory layout after extraction:
```
X-Plane 12\Resources\plugins\xp_wellys_atc\
├── win_x64\
│   └── xp_wellys_atc.xpl          (self-contained — libcurl linked statically)
└── data\
    ├── settings.json
    ├── models_catalog.json
    ├── atc_prompt_templates.json
    ├── vrps\airport_vrps.json
    └── atc_profiles\eu\ us\
```

> No `Resources\models\`, no `espeak-ng-data`, no extra DLLs — cloud mode
> needs none of them.

Launch X-Plane, open the **ATC** menu → **Settings** tab, pick **OpenAI**
or **Mistral** as the backend, paste your API key, and click **Save Key**.

---

## Build from source

### Recommended: GitHub Actions

No cross-compile and no local Windows toolchain is needed. The
`windows-latest` runner has MSVC, the Windows SDK, and vcpkg. Trigger a
build and download the artifact:

```bash
make ci-remote      # push current branch + dispatch the build workflow
make win-artifact   # download xp_wellys_atc-win -> dist-win/
```

The CI configures with local inference off:
```
cmake -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static \
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded \
  -DXPWELLYS_USE_LOCAL_INFERENCE=OFF \
  -DBUILD_TESTS=OFF
cmake --build build-win --config Release --target xp_wellys_atc
```

### Native build on a Windows dev machine

If you have Visual Studio 2022 (MSVC), CMake, Ninja and vcpkg installed:

```powershell
# 1. Dependencies (from the repo root)
#    - X-Plane SDK headers + Libraries\Win\*.lib -> sdk\
#    - Dear ImGui                                 -> vendor\imgui\
#    - nlohmann/json                              -> vendor\json.hpp
#    (see the "Set up dependencies" step in .github\workflows\build.yml
#     for the exact download commands)

# 2. Static libcurl with Schannel TLS
vcpkg install curl:x64-windows-static

# 3. Configure + build (from a "x64 Native Tools" prompt or after msvc-dev-cmd)
cmake -B build-win -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
  -DXPWELLYS_USE_LOCAL_INFERENCE=OFF `
  -DBUILD_TESTS=OFF
cmake --build build-win --config Release --target xp_wellys_atc
```

The result is `build-win\xp_wellys_atc.xpl`. Copy it into
`…\plugins\xp_wellys_atc\win_x64\` next to a copy of the repo's `data\`
folder.

> The X-Plane SDK's Windows import libs (`XPLM_64.lib`, `XPWidgets_64.lib`)
> are downloaded by `make setup` on macOS/Linux and by the CI's setup
> step on Windows.

---

## API key storage (Windows)

On macOS the plugin uses the system Keychain; on Linux, permission-restricted
files. On Windows, keys are stored in the **Windows Credential Manager**
(generic credentials, encrypted per-user via DPAPI):

```
Control Panel → Credential Manager → Windows Credentials
├── com.xp_wellys_atc.openai/default
└── com.xp_wellys_atc.mistral/default
```

The two backends use separate entries, so switching modes never requires
re-pasting. Keys are never written to `settings.json` or `Log.txt` (only
the last 4 characters appear in logs for audit purposes). Manage them from
the Settings tab's **\[Paste\] / Save Key / Delete Key** buttons —
`Ctrl+V` is intercepted by X-Plane's command bindings, so use the
**\[Paste\]** button, which reads the clipboard natively via the Win32
API (no external tool needed).

---

## Known issues

| Issue | Cause | Fix |
|---|---|---|
| Recording stays silent / no transcript | Windows mic privacy blocks the app | Settings → Privacy & security → Microphone → allow desktop apps to access the microphone |
| `Ctrl+V` does nothing in the API-key field | X-Plane grabs `Ctrl+V` as a command binding | Use the **\[Paste\]** button in the Settings tab |

Windows 10+ has no in-process microphone permission prompt (unlike macOS);
a blocked device simply yields an empty capture stream.

---

## Technical notes

- **Zero extra DLLs.** libcurl is linked statically (vcpkg
  `curl:x64-windows-static`, TLS via **Schannel**), so the `.xpl` is
  self-contained. `XPLM_64.lib` / `XPWidgets_64.lib` are link-time only —
  X-Plane provides XPLM at runtime.
- **Microphone capture** uses **WASAPI** shared-mode event-driven capture
  (`audio_input_wasapi.cpp`). The device mix format (typically 32-bit
  float, 44.1/48 kHz, stereo) is downmixed to mono and streaming-resampled
  to 16 kHz 16-bit PCM in-process — the same format the macOS (CoreAudio)
  and Linux (PulseAudio) backends produce.
- **SHA256** (model-manifest verification, dormant in cloud mode) uses
  Windows **CNG / BCrypt**.
- The `.xpl` is a renamed MSVC MODULE (DLL); X-Plane 12 loads
  `win_x64\<pluginname>.xpl`.
