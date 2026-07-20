# Third-Party Dependencies

This document lists every third-party component bundled with, statically
linked into, or dynamically loaded by `xp_wellys_atc`. The plugin itself
is licensed under **GNU GPL-3.0-or-later**; the table below shows that
all dependencies are GPLv3-compatible.

## At-a-glance

| Component | Version | License | How it ships |
|---|---|---|---|
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | v1.8.5-95 (`f049fff9`) | MIT | Static library inside the `.xpl` (prebuilt, see below) |
| [llama.cpp](https://github.com/ggerganov/llama.cpp) | gguf-v0.19.0-687 (`f449e055`) | MIT | Static library inside the `.xpl` (prebuilt, see below) |
| [Piper](https://github.com/OHF-Voice/piper1-gpl) | v1.4.2 | MIT (libpiper); GPL-3.0 (espeak-ng) | `libpiper.dylib` next to the `.xpl` |
| [onnxruntime](https://github.com/microsoft/onnxruntime) | 1.22.0 | MIT | `libonnxruntime.1.22.0.dylib` next to the `.xpl` (prebuilt vendor binary) |
| [espeak-ng](https://github.com/espeak-ng/espeak-ng) | bundled with Piper | GPL-3.0-or-later | Statically linked inside `libpiper.dylib`; data dir bundled |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.9 | MIT | Compiled into the `.xpl` |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | Header-only, compiled into the `.xpl` |
| [Catch2](https://github.com/catchorg/Catch2) | v3.7.1 | BSL-1.0 | Test-only; not shipped with the release |
| libcurl | system (`/usr/lib/libcurl.4.dylib`) | curl/MIT-style | macOS-system; dynamically loaded |
| X-Plane SDK | XPSDK430 | freely redistributable for plugin development | Headers only at build time |

## Per-component detail

### whisper.cpp — MIT

Inference engine for the bundled Whisper STT model. Compiled with the
Metal backend enabled (`GGML_METAL=ON`, `GGML_METAL_EMBED_LIBRARY=ON`)
and the Apple Accelerate framework on macOS-arm64, plain CPU on Linux.
Built in `rwellinger/xp_wellys_libs` and linked statically into the
plugin module from that repo's prebuilt bundle (see *Source
availability*).

The Whisper *model files* are downloaded by the user at first launch
from [`huggingface.co/ggerganov/whisper.cpp`](https://huggingface.co/ggerganov/whisper.cpp);
both are licensed under MIT in line with whisper.cpp itself.

- `ggml-small.en-q5_1.bin` — English-only, used by the EU and US ATC profiles.
- `ggml-small-q5_1.bin` — multilingual, used by the DE ATC profile (added in M6).

The Models tab filters by `settings::backend_language()` so only the
file matching the active profile is downloaded by default; a *Show all
languages* toggle exposes both.

### llama.cpp — MIT

Inference engine for the bundled Llama LLM model. Same Metal/Accelerate
setup as whisper.cpp; built in `rwellinger/xp_wellys_libs` and linked
statically from its prebuilt bundle. The plugin uses the public `llama`
target (`llama_lm.cpp` includes only `llama.h`).

The bundled *model* (`Llama-3.2-3B-Instruct-Q4_K_M.gguf`) is licensed
under the **Llama 3.2 Community License Agreement** by Meta, accepted
when downloading the file on first launch. Read it at
<https://www.llama.com/llama3_2/license/>. Key terms: free for
commercial and non-commercial use, but redistribution requires the
same license + attribution. The plugin neither redistributes nor
modifies the weights — it downloads them straight from HuggingFace
to the user's disk.

### Piper — MIT (libpiper) + GPL-3.0 (espeak-ng inside)

Neural TTS used for the ATC voice. `libpiper` itself is MIT-licensed;
its phonemizer dependency `espeak-ng` is GPL-3.0-or-later and is
statically linked into `libpiper.dylib`. Because espeak-ng is GPLv3
and is linked into a binary we ship, the combined Piper artifact is
effectively GPLv3 — same license as this plugin, so no conflict.

Piper is built in `rwellinger/xp_wellys_libs`, whose CMake drives
libpiper's own build: espeak-ng is fetched via ExternalProject and
statically linked, and the prebuilt onnxruntime release for the target
platform is downloaded. Both `libpiper` and onnxruntime ship inside the
bundle this repo consumes.

The bundled *voice models* come from the
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices)
collection. Each voice ships as a paired `.onnx` + `.onnx.json` and is
downloaded by the user at first launch:

- `en_US-lessac-medium` — English, used by the EU and US ATC profiles
  (and as Piper's neutral baseline voice). Additional English per-role
  voices (`en_US-ryan-high`, `en_US-amy-medium`, `en_GB-alan-medium`,
  and optional rows) ship from the same collection.

The Piper voice models are MIT-licensed
(see <https://github.com/rhasspy/piper/blob/master/VOICES.md>); the
underlying recordings are CC0 / public-domain. The required per-role
voices are downloaded by default; a *Show all languages* toggle exposes
the optional rows.

The `espeak-ng-data/` directory (~19 MB of phonemizer dictionaries)
ships **inside the plugin bundle** at
`<plugin>/Resources/espeak-ng-data/` so users do not need to install
espeak-ng system-wide.

### onnxruntime — MIT

Microsoft's neural-network inference runtime. Used by Piper. We ship the
**prebuilt binary** released by Microsoft on GitHub — building
onnxruntime from source is a multi-day undertaking and not realistic for
this project. It is fetched by libpiper's CMake while the
`xp_wellys_libs` bundle is built and travels inside that bundle:
`libonnxruntime.1.22.0.dylib` (~33 MB) on macOS-arm64,
`libonnxruntime.so.1.22.0` plus its `.so.1`/`.so` symlinks and
`libonnxruntime_providers_shared.so` on Linux. Either way it is staged
alongside the `.xpl` and resolves through the `@loader_path` / `$ORIGIN`
rpath at runtime.

### Dear ImGui — MIT

In-sim UI library. Compiled directly into the plugin `.xpl` (no
shared lib). Vendored under `vendor/imgui/` by `make setup`.

### nlohmann/json — MIT

Header-only JSON parser. Used for `settings.json`, ATC templates,
flight rules, airport VRPs. Vendored under `vendor/json.hpp`.

### Catch2 — BSL-1.0

Unit-test framework. Test-only dependency: not built into the
release artifact. Vendored under `vendor/catch2/`.

### libcurl — curl/MIT-style

Used by the in-plugin model downloader (HTTPS GET with `Range` resume
to HuggingFace). Plugin links against the system libcurl on macOS
(`/usr/lib/libcurl.4.dylib`); not redistributed.

### X-Plane SDK

Laminar Research's plugin SDK (`XPLM430`). Headers + framework symbols
under `sdk/`, populated by `make setup`. The SDK is freely
redistributable as part of plugins per
<https://developer.x-plane.com/sdk/>; the plugin links the
`XPLM.framework` and `XPWidgets.framework` directly.

## Why GPL-3.0 for the plugin?

The plugin is licensed under GPL-3.0-or-later because espeak-ng
(GPL-3.0-or-later) is statically linked into the `libpiper.dylib` we
ship. Linking GPLv3 code into a non-GPLv3 binary would conflict, so the
plugin itself adopts GPLv3 to stay compatible.

All other bundled / linked components above are GPLv3-compatible (MIT,
BSD-style, BSL-1.0, freely redistributable SDK).

## Source availability

This plugin is open source. The full source is at
`https://github.com/rwellinger/xp_welly_llm_atc`. In line with GPLv3,
binary releases include or link to the source repository in the
release notes.

For the bundled third-party libraries (whisper.cpp, llama.cpp, Piper,
espeak-ng), the source is publicly available at the project URLs listed
in the table above. This repository no longer vendors them as submodules:
they are compiled once in the sibling repository
[`rwellinger/xp_wellys_libs`](https://github.com/rwellinger/xp_wellys_libs)
and consumed here as a prebuilt binary bundle, pinned by the
`PREBUILT_LIBS_VERSION` file at the repository root.

That repository is public and holds both the exact upstream revisions (as
submodule pins) and the complete scripts used to build the binaries — the
corresponding source and installation information GPLv3 requires for the
espeak-ng code we distribute in binary form. The revisions a given plugin
build actually links are additionally recorded in the `[submodule pins]`
section of the consumed bundle's `manifest.txt`. As of
`PREBUILT_LIBS_VERSION` 0.3.0 they are:

| Component | Revision |
|---|---|
| whisper.cpp | `f049fff95a089aa9969deb009cdd4892b3e74916` (v1.8.5-95) |
| llama.cpp | `f449e0553708b895adbd94a301431cef691f632d` (gguf-v0.19.0-687) |
| piper1-gpl | `d6975e21a440c0d8b6e5fb7c41027409af13d44d` (v1.4.2) |
| onnxruntime | 1.22.0 (prebuilt upstream binary, fetched by Piper's CMake) |

Two obligations follow from this and must be honoured for as long as any
release is in circulation:

- **`rwellinger/xp_wellys_libs` must remain public.** It is this document's
  source offer; taking it private would leave the espeak-ng binaries we
  ship without one.
- **A bundle release asset must not be deleted** while any plugin version
  still pins it, for the same reason.
