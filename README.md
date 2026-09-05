# JenyaDereverb2

Real-time neural de-reverb for spoken voice — podcasts, dialogue, voice-over. Runs locally
with the embedded DPDFNet model; no cloud, no account. macOS VST3 and Audio Unit.

![JenyaDereverb2](docs/images/jenya-dereverb-ui.png)

## Install

Prebuilt universal (Apple Silicon + Intel) bundles are in [`Release/`](Release) and on the
[releases page](../../releases).

- **VST3** → a folder your host scans, commonly `~/Library/Audio/Plug-Ins/VST3/`
- **Audio Unit** → `~/Library/Audio/Plug-Ins/Components/`

Quit and reopen the host, then rescan. The bundles are ad-hoc signed, so macOS may need
quarantine metadata cleared after download.

## Requires a 48 kHz project

The model runs at 48 kHz only. At any other rate the plug-in loads, shows its interface and
responds to the knob — but passes audio through untouched. The status line says which:

| Status line | Meaning |
|---|---|
| `DPDFNET AI • 20 ms` (green) | Model running |
| `BYPASSED • NEEDS 48 kHz, HOST IS 44.1 kHz` (red) | Passthrough — change the project rate |
| `BYPASSED • MODEL FAILED TO LOAD` (red) | Model did not initialise |

## Use

**Mix** is the only control, 100% by default. It ramps over 20 ms so it is click-free when
automated, and at 0% the output is bit-identical to the delay-compensated input. Latency is
960 samples (20 ms at 48 kHz).

**Partial Mix can cancel.** The model reconstructs spectral phase, so dry and wet are not
phase-coherent: about half of all bins sit more than 90° apart, and individual bins can null
at intermediate settings. 0% and 100% are unaffected — prefer full-wet, or automate between
the two.

## Build

macOS, Xcode command-line tools, CMake 3.22+, C++17. JUCE 8.0.4 is fetched during
configuration; the model and ONNX Runtime are included.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-release --config Release -j 4 --target ClearRoom_All
```

Outputs land in `build-release/ClearRoom_artefacts/Release/{VST3,AU}/`. To run the DSP
regression suite:

```sh
cmake --build build-release --config Release -j 4 --target ClearRoomTests
./build-release/ClearRoomTests_artefacts/Release/ClearRoomTests
```

`ClearRoomHostCheck <bundle>` loads a built bundle the way a DAW does and reports where it
fails; `scripts/show-engine-log.sh` prints the plug-in's lifecycle trace from the macOS
unified log, which works even inside a sandboxed host.

## Compatibility

VST3 and Audio Unit, version 0.5.0. macOS universal (`arm64`, `x86_64`). Mono, stereo and
matching multichannel layouts through 16 channels, including 5.1 for Fairlight.

## Licences

DPDFNet model (Apache-2.0) and ONNX Runtime (MIT). Earlier WPE research code is retained in
`Source/OnlineWpe.*` but is not compiled. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`docs/OPEN_SOURCE_REVIEW.md`](docs/OPEN_SOURCE_REVIEW.md).

De-reverberation cannot restore what was never captured; very distant or clipped recordings
may still produce artifacts. Keep the original.
