# JenyaDereverb2

JenyaDereverb2 is a compact, real-time neural VST3 for making reverberant spoken voice
drier and more suitable for podcasts, dialogue, and voice-over work. It embeds the
DPDFNet speech-enhancement model and runs locally—no cloud service or account is needed.

![JenyaDereverb2 running in DaVinci Resolve](docs/images/jenya-dereverb-ui.png)

## Download and install

Ready-to-use universal macOS plug-ins are included in [`Release/`](Release), in both VST3
and Audio Unit form. Both support Apple Silicon and Intel Macs.

1. Download or clone this repository.
2. Copy `Release/JenyaDereverb2.vst3` to `~/Library/Audio/Plug-Ins/VST3/`, and/or
   `Release/JenyaDereverb2.component` to `~/Library/Audio/Plug-Ins/Components/`.
3. Restart the DAW. In DaVinci Resolve, rescan audio plug-ins if it does not appear.

### Which format does DaVinci Resolve need?

This matters, because the two Resolve distributions differ:

- **Resolve from the Mac App Store** runs in a macOS sandbox and **does not load VST3 at
  all** — it scans Audio Units only. Install the `.component`. A VST3 will simply never
  appear, with no error shown.
- **Resolve downloaded from blackmagicdesign.com** is not sandboxed and loads both. Either
  format works.

To tell which one you have, run:

```sh
ls "/Applications/DaVinci Resolve.app/Contents/_MASReceipt" 2>/dev/null \
  && echo "App Store build - use the Audio Unit" \
  || echo "Direct download - either format works"
```

The release is ad-hoc signed. macOS may require a locally trusted signature or removal
of downloaded-file quarantine metadata before third-party hosts can scan it.

## Use

JenyaDereverb2 has one control:

- **Mix** blends latency-aligned original audio with neural processing. It defaults to
  100%. It is ramped over 20 ms, so it can be automated or dragged without clicks, and at
  0% the output is bit-identical to the (delay-compensated) input.

Note on intermediate Mix settings: the model reconstructs spectral phase rather than
applying a magnitude mask, so dry and wet are not phase-coherent. Around half of all
spectral bins sit more than 90 degrees apart, and individual bins can therefore cancel at
partial Mix. This is inherent to blending any phase-modifying spectral processor and is
audible as mild hollowness in the middle of the knob's range; 0% and 100% are unaffected.
Prefer full-wet, or automate between 0% and 100%, if you hear it.

### The project must run at 48 kHz

The neural model runs at 48 kHz only. At any other rate the plug-in passes audio through
untouched — it will load, show its interface and respond to the knob, but do nothing.

The status line at the bottom left of the interface says which state it is in:

- `DPDFNET AI  •  20 ms` (green dot) — the model is running.
- `BYPASSED  •  NEEDS 48 kHz, HOST IS 44.1 kHz` (red dot) — passthrough; change the
  project rate.
- `BYPASSED  •  MODEL FAILED TO LOAD` (red dot) — the embedded model did not initialise.

In DaVinci Resolve, set the rate under **Project Settings > Fairlight > Audio >
Sample Rate** to 48 kHz. Reported latency is 960 samples (20 ms at 48 kHz).

## Build from source

Requirements: macOS, Xcode command-line tools, CMake 3.22+, Git, and a C++17 compiler.
JUCE 8.0.4 is fetched during configuration. The model, ONNX Runtime headers, and stripped
universal ONNX Runtime library needed by the build are included.

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-release --config Release -j 4 --target ClearRoom_VST3
```

The outputs are `build-release/ClearRoom_artefacts/Release/VST3/JenyaDereverb2.vst3` and
`build-release/ClearRoom_artefacts/Release/AU/JenyaDereverb2.component`. Build the Audio
Unit with the `ClearRoom_AU` target, or both with `ClearRoom_All`.

```sh
cmake --build build-release --config Release -j 4 --target ClearRoomTests
./build-release/ClearRoomTests_artefacts/Release/ClearRoomTests
```

## Compatibility

- VST3 and Audio Unit effect, version 0.5.0
- macOS universal binary (`arm64` and `x86_64`)
- Mono, stereo, and matching multichannel layouts through 16 channels
- Tested for construction and 5.1 layout support expected by Fairlight

## Open-source components

The neural path uses the Apache-2.0-licensed DPDFNet model and MIT-licensed ONNX Runtime.
Earlier WPE research code is retained in `Source/OnlineWpe.*` but is no longer compiled. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)
and [`docs/OPEN_SOURCE_REVIEW.md`](docs/OPEN_SOURCE_REVIEW.md).

## Important limitation

De-reverberation cannot reconstruct information that was never captured. Very distant,
clipped, or extremely reverberant recordings may still produce artifacts. Always keep
the original recording.
