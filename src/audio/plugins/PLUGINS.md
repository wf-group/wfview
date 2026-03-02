# TX Audio Processing Plugins

These files implement three classic LADSPA DSP algorithms as standalone C++ classes,
independent of LADSPA, FFTW, or any external audio framework.

---

## Files

| File | Description |
|------|-------------|
| `dysoncompress.h` | `DysonCompressor` class header |
| `dyson_compress.cpp` | `DysonCompressor` implementation |
| `mbeq.h` | `MbeqProcessor` class header |
| `mbeq.cpp` | `MbeqProcessor` implementation |
| `noisegate.h` | `NoiseGate` class header |
| `noisegate.cpp` | `NoiseGate` implementation |
| `dyson_compress_1403.xml` | Original LADSPA metadata (reference only) |
| `mbeq_1197.xml` | Original LADSPA metadata (reference only) |
| `mbeq_1197.so.c` | Original LADSPA C source (reference only) |
| `gate_1921.xml` | Original LADSPA metadata (reference only) |
| `gate_1921.so.c` | Original LADSPA C source (reference only) |

---

## DysonCompressor

**Origin:** LADSPA plugin #1403, algorithm by John S. Dyson (BSD 2-clause, 1996).

**Class:** `DysonCompressor` (in `dysoncompress.h`)

### Parameters

| Setter | Range | Default | Description |
|--------|-------|---------|-------------|
| `setPeakLimit(float dB)` | −30 … 0 | −10 | Target peak output level |
| `setReleaseTime(float s)` | 0.01 … 1.0 | 0.1 | AGC release time constant |
| `setFastRatio(float r)` | 0.0 … 1.0 | 0.5 | Fast-path compression ratio exponent |
| `setSlowRatio(float r)` | 0.0 … 1.0 | 0.3 | Slow-path (master) gain ratio exponent |

### Key methods

```cpp
DysonCompressor comp(48000.0f);       // construct at sample rate
comp.setPeakLimit(-10.0f);
comp.process(inPtr, outPtr, nSamples);
float gain = comp.getLastTotalGain(); // linear, 0–1; for gain-reduction meter
comp.reset();                         // clear all filter state
```

### Porting notes (vs. original LADSPA)

- All `plugin_data->` struct references replaced with `this->` class members.
- `void*` input/output ports replaced with typed `const float* in, float* out`.
- `goto skipagc` replaced with a plain `if` block (avoids jumping over C++ declarations).
- `ladspa-util.h` macros (`DB_CO`, etc.) inlined directly — no external header needed.
- `sample_count` parameter renamed to `nSamples` and typed `unsigned long`.
- `cleanup()` replaced by a proper destructor using `std::free()`.
- `getLastTotalGain()` added for gain-reduction metering.

---

## MbeqProcessor (Multiband EQ)

**Origin:** LADSPA plugin #1197, by Steve Harris (GPL).
Band range remapped from 50 Hz–20 kHz to **50 Hz–8 kHz** for voice TX use.
FFT back-end replaced from `rfftw` (FFTW v2, obsolete) to `Eigen::FFT<float>`.

**Class:** `MbeqProcessor` (in `mbeq.h`)

### Band centre frequencies

| Band | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|------|---|---|---|---|---|---|---|---|---|---|----|----|----|----|-----|
| Hz   | 50 | 100 | 200 | 300 | 400 | 600 | 800 | 1k | 1.2k | 1.6k | 2k | 2.5k | 3.2k | 5k | 8k |

### Parameters

| Setter | Range | Default |
|--------|-------|---------|
| `setBand(int idx, float gainDB)` | −70 … +30 dB | 0 (flat) |

### Key methods

```cpp
MbeqProcessor eq(48000.0f);
eq.setBand(7,  +3.0f);   // 1 kHz +3 dB
eq.setBand(14, -6.0f);   // 8 kHz −6 dB
eq.process(inPtr, outPtr, nSamples);
```

### Algorithm

Overlap-add OLA with Hann window, FFT_LEN = 1024, OVER_SAMP = 4 (75 % overlap).
Per-block latency: FFT_LEN − FFT_LEN/OVER_SAMP = **768 samples**.

#### Normalization

`Eigen::FFT` normalizes its **inverse** transform by 1/N (unlike FFTW v2 `rfftw`,
which was unnormalized with round-trip scale = N).  With Eigen the round-trip is
unity, so the OLA accumulation divides only by `OVER_SAMP` (not `FFT_LEN × OVER_SAMP`),
giving a scale factor of `4 / 4 = 1.0` at 0 dB.  The original FFTW scale
`4 / (FFT_LEN × OVER_SAMP) = 1/1024` would produce silence (−60 dB) with Eigen.

#### Hermitian symmetry

Real-valued gain coefficients are applied to both the positive-frequency bin `i`
and its conjugate mirror `FFT_LEN − i` to keep the IFFT output real:

```cpp
freqBuf[i]           *= coefs[i];
freqBuf[FFT_LEN - i] *= coefs[i];
```

The DC bin is always zeroed; the Nyquist bin uses `coefs[FFT_LEN/2 − 1]`.

### Porting notes (vs. original LADSPA)

- `rfftw` API removed entirely — replaced with `Eigen::FFT<float>` (unsupported module).
- Band table remapped: top band 8 kHz instead of 20 kHz; 15 bands chosen for voice clarity.
- All LADSPA port pointers and `plugin_data` struct removed; state is class members.

---

## NoiseGate

**Origin:** LADSPA plugin #1921 ("gate") by Steve Harris (GPL).
Stereo variant removed — TX audio is mono.  Key-filter output-select port
(key-listen / gate / bypass) not exposed; always runs in gate mode.

**Class:** `NoiseGate` (in `noisegate.h`)

**Position in chain:** Applied **before** input gain, on the raw microphone signal.

### Parameters

| Setter | Range | Default | Description |
|--------|-------|---------|-------------|
| `setThreshold(float dB)` | −70 … 0 | −40 | Level at which the gate opens |
| `setAttack(float ms)` | 0.01 … 1000 | 10 | Time for gate to fully open |
| `setHold(float ms)` | 2 … 2000 | 100 | Minimum time gate stays open |
| `setDecay(float ms)` | 2 … 4000 | 200 | Time for gate to fully close |
| `setRange(float dB)` | −90 … 0 | −90 | Attenuation when gate is closed |
| `setLfCutoff(float hz)` | 20 … 4000 | 80 | Key detector highpass frequency |
| `setHfCutoff(float hz)` | 200 … 20000 | 8000 | Key detector lowpass frequency |

### Key methods

```cpp
NoiseGate gate(48000.0f);          // construct at sample rate
gate.setThreshold(-40.0f);
gate.process(inPtr, outPtr, nSamples);
float g = gate.getGain();          // 0.0 = fully closed, 1.0 = fully open
gate.reset();                      // clear envelope and filter state
```

### Algorithm

State machine with four states — CLOSED → OPENING → OPEN → CLOSING:

- **Key detector:** The input is passed through a low-shelf biquad (LF cut, −40 dB gain,
  acting as highpass) then a high-shelf biquad (HF cut, −50 dB gain, acting as lowpass).
  This bandpass key signal is envelope-followed to drive the state machine.
- **Envelope follower:** Peak-hold with slow exponential release (τ = `ENV_TR` = 0.0001).
- **Gate coefficient:** Linear ramp from 0 → 1 during OPENING, 1 → 0 during CLOSING.
- **Output mix:** `in * (range_linear * (1 − gate) + gate)` — fully open passes signal
  unmodified; fully closed attenuates by `range` dB.

### Porting notes (vs. original LADSPA)

- LADSPA `plugin_data` struct and port pointers replaced by class members.
- `biquad.h` / `ladspa-util.h` inlined directly — no external headers.
- `lf_fc` / `hf_fc` changed from fraction-of-sample-rate to Hz; conversion to radians
  happens inside the shelving-filter coefficient helpers.
- `output select` port removed; always operates in normal gate mode (op = 0).
- Stereo variant (`stereo_gate`, id 1922) not ported — not needed for mono TX audio.
- `getGain()` added for optional gate-activity metering.

---

## Dependencies

### Eigen (required for MbeqProcessor)

`Eigen::FFT` lives in Eigen's **unsupported** module, which ships with the standard
Eigen3 package — no separate download is needed.

#### Install on Debian / Ubuntu

```bash
sudo apt-get install libeigen3-dev
```

The package installs headers to `/usr/include/eigen3/`.
`mbeq.cpp` already uses the correct Linux path:

```cpp
#ifdef Q_OS_LINUX
#  include <eigen3/unsupported/Eigen/FFT>   // ← installed by libeigen3-dev
#else
#  include <unsupported/Eigen/FFT>
#endif
```

#### Verify

```bash
dpkg -l libeigen3-dev        # check if installed
ls /usr/include/eigen3/unsupported/Eigen/FFT   # confirm FFT header exists
```

#### Eigen::FFT back-end

By default `Eigen::FFT` uses the built-in "Teensy" FFT implementation —
**no additional libraries** (not even FFTW) are required.
If you later want FFTW performance, define `EIGEN_FFTW_DEFAULT` before the include
and link `-lfftw3f`, but this is not necessary for voice-quality TX audio.

---

## Thread safety

Both classes are **not thread-safe** on their own.
`TxAudioProcessor` (the caller) snapshots its parameters under a mutex and calls
`process()` exclusively from the converter thread, so no additional locking is needed
inside these classes.

---

## UI — Audio Processor settings window

| Role | File | Class |
|------|------|-------|
| Dialog (UI, slots, meters) | `src/audioprocessingwidget.cpp` / `include/audioprocessingwidget.h` | `AudioProcessingWidget` |
| Parameter bridge | `src/wfmain.cpp` — `on_audioProcBtn_clicked()`, `onAudioProcPrefsChanged()`, `applyAudioProcPrefs()` | `wfmain` |
| DSP engine | `src/audio/txaudioprocessor.cpp` / `include/txaudioprocessor.h` | `TxAudioProcessor` |
| Level meter widget | `src/meter.cpp` / `include/meter.h` | `meter` |

### Window lifetime

`AudioProcessingWidget` is created lazily when the user first clicks the
**Audio Processing** button (`on_audioProcBtn_clicked()`, `src/wfmain.cpp`).
`TxAudioProcessor` is created when the radio connection is set up
(same file, just before `makeRig()`).  Both paths check whether the other
object already exists and connect the level-meter signals at that point, so
the window can be opened either before or after connecting to a radio.

### Meter signals

The three meters at the bottom of the dialog are driven by signals emitted
from the converter thread inside `TxAudioProcessor::processAudio()`:

| Signal | Slot | Meter |
|--------|------|-------|
| `txInputLevel(float peak)` | `updateInputLevel(float)` | Input (meterAudio) |
| `txOutputLevel(float peak)` | `updateOutputLevel(float)` | Output (meterAudio) |
| `txGainReduction(float linear)` | `updateGainReduction(float)` | Gain Reduction (meterComp) |

Peaks are in the range 0.0–1.0 and are scaled to 0–255 for `meter::setLevel()`.
The gain-reduction value is converted from linear (1.0 = no reduction) to dB and
then mapped to 0–255 (0 dB → 0, −20 dB → 255).

**Important:** `meter::paintEvent()` returns immediately if `haveExtremities` is
false (no scale data).  `meter::setMeterExtremities(lo, hi, red)` must be called
after `setMeterType()` to enable drawing.  For `meterAudio` the bar-drawing path
uses the `audiopot` log-taper LUT directly (ignores `lo`/`hi`), so any valid
call works (e.g. `0, 255, 241`).  For `meterComp` the linear bar path uses
`scaleMin`/`scaleMax` for pixel mapping, so pass the same 0–255 range.

---

## Audio pipeline — where the plugins are inserted

```
Microphone (QAudioSource)
    ↓
audioHandlerBase::init()          src/audio/audiohandlerbase.cpp
    Sets processingHook on the audio converter when
    setup.isinput && setup.txProc is non-null.
    ↓
audioConverter  (TimeCriticalPriority thread)
    src/audio/audioconverter.cpp
    1. Decode / format-convert
    2. Resample to radio's sample rate
    3. ── processingHook ──────────────────────────────────────────
          TxAudioProcessor::processAudio(samples, sampleRate)
              a. Snapshot parameters (QMutex, brief)
              b. Apply input gain
              c. emit txInputLevel  → Input meter
              d. Run EQ (MbeqProcessor) and/or Compressor (DysonCompressor)
                 in configured order (EQ→Comp or Comp→EQ)
              e. emit txGainReduction → GR meter
              f. Apply output gain, clip to [-1, 1]
              g. emit txOutputLevel  → Output meter
              h. emit haveSidetoneFloat → sidetone mix into RX stream
    4. Encode (Opus / PCM / ADPCM)
    ↓
Radio UDP TX path  (icomUdpAudio / yaesuUdpAudio / rtpAudio)
```

The hook is installed via a `QMetaObject::invokeMethod` queued call so that it
runs on the converter's own thread, avoiding any lock-order issues.  Plugin
objects (`MbeqProcessor`, `DysonCompressor`) are owned by `TxAudioProcessor`
and are (re)created whenever the sample rate changes.
