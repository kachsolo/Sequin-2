# Sequin 2 — DSP prototype v0.1 (UNVERIFIED)

Fresh JUCE VST3 experiment for Windows/Cubase. This is **not** a validated production plugin.

## Build without local compiler
Create a new GitHub repository, upload the contents of this folder, open **Actions → Build Sequin 2 Prototype → Run workflow**, then download the Windows artifact if the build succeeds. Do not overwrite your working Sequin VST3; this uses a new plugin identifier.

## What is implemented
- Normalized time-domain autocorrelation pitch estimation, 75–900 Hz (monophonic voiced material only).
- Candidate harmonics at integer multiples of detected F0; no arbitrary-bin harmonic counting.
- Neighbor-harmonic comparison, per-harmonic excess and independent gain reductions.
- STFT Hann-window overlap-add, 4096-point, 1024 hop, linked stereo, latency reported as 4096 samples.
- Sensitivity, maximum cut, low/high focus, mix, bypass, simple diagnostics UI.

## Known limitations / experimental warnings
- F0 tracker is rudimentary and can make octave errors or miss breathy voices; unvoiced material intentionally receives no cuts.
- Comparing immediate neighbors can still mistake real formants for excess. No learned spectral envelope, long-term phrase context, or transient masking yet.
- Fixed spectral windows may cause artifacts; gain smoothing, proper stereo analysis, and better voicing logic are next.
- No offline audio null test, DAW test, build verification, or listening validation has been completed.
- FFT implementation, inverse scaling, dry/wet alignment, and latency require explicit testing before use on sessions.
- Audio callback scratch capacity assumes DAW does not exceed prepareToPlay's maximum block size.

## Safe testing
Use a copy of a Cubase session. First test bypass and 0% wet for perfect transparency, then test single sine, harmonic series, synthetic vowels, and real vocals. Record dry and wet, measure FFT difference and phase alignment, and check for clicks at note transitions.
