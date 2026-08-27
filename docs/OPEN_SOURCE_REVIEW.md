# Open-source review (2026-08-28)

Star counts are snapshots, not quality guarantees; releases, tests, maintenance,
licensing, and problem fit were weighted more heavily.

| Project | Adoption / reliability signal | License | Fit and decision |
|---|---|---|---|
| DeepFilterNet | ~4.1k stars; peer-reviewed; real-time LADSPA implementation; several model generations | MIT or Apache-2.0 | Strong future neural-engine candidate. It adds Rust/model packaging and model distribution complexity, so it is not embedded. |
| NARA-WPE | 569 stars, 288 commits, 168 forks; published and tested online/offline implementations | MIT | Selected for v0.2. The recursive online formulation was adapted from NumPy to bounded, allocation-free C++ with JUCE FFT processing. |
| Noise Repellent | 542 stars, 1,110 commits, installers, CI, 40 forks | GPL-3.0-or-later | Strong JUCE/FFT reference, but it targets broadband noise. Its transient veto, smoothed masks, residual listening, and state handling are useful patterns. No code copied. |
| werman/noise-suppression-for-voice | Established cross-platform releases and broad community use | GPL-3.0 | Reliable RNNoise VST reference, but restricted to 48 kHz and aimed at noise/VAD. No code copied. |
| NoiseTorch | 10.3k stars, 251 forks | GPL-family | Strong evidence for RNNoise deployment maturity, but it is a Linux microphone app, not de-reverb DSP. |
| VX Studio / VXDeverb | 0 stars, 0 forks, no releases; self-reported passing regression suite and macOS builds | Verify before reuse | LRSV/RT60/WPE is relevant, but adoption and independent validation are absent. Used only as a research pointer; no code copied. |
| AuClear | New JUCE/ONNX restoration rack; advertises DeepFilterNet integration | Verify before reuse | Useful packaging reference, but insufficient adoption/release evidence to call proven. No code copied. |

## Resulting design choices

- Keep the audio thread allocation- and lock-free.
- Smooth adaptive gain changes and protect direct transients.
- Preserve low-frequency speech body separately from late-tail reduction.
- Support arbitrary host sample rates instead of silently assuming 48 kHz.
- Save parameters through JUCE APVTS and provide wet/dry auditioning.
- Keep the current engine dependency-light. A future optional DeepFilterNet backend should
  run inference off the audio thread, bundle a versioned model, compensate latency, and
  retain this DSP engine as a zero-latency fallback.

No third-party runtime or model weights are included. The NARA-WPE attribution and MIT
license are retained in `THIRD_PARTY_NOTICES.md`.
