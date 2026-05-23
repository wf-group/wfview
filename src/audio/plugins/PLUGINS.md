# Audio Processing Plugins

These files implement classic LADSPA DSP algorithms as standalone C++ classes,
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
| `triple_para.h` | `TriplePara` class header |
| `triple_para.cpp` | `TriplePara` implementation |
| `dyson_compress_1403.xml` | Original LADSPA metadata (reference only) |
| `mbeq_1197.xml` | Original LADSPA metadata (reference only) |
| `mbeq_1197.so.c` | Original LADSPA C source (reference only) |
| `gate_1921.xml` | Original LADSPA metadata (reference only) |
| `gate_1921.so.c` | Original LADSPA C source (reference only) |
| `triple_para_1204.xml` | Original LADSPA metadata (reference only) |
| `triple_para_1204.so.c` | Original LADSPA C source (reference only) |

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

## TriplePara (4-Band Parametric EQ with Shelves)

**Origin:** LADSPA plugin #1204 ("triplePara") by Steve Harris (GPL).
Reduced from 5 bands to 4 bands for RX audio EQ use.

**Class:** `TriplePara` (in `triple_para.h`)

**Position in chain (RX):** Applied **after** noise reduction, before output gain.

### Band layout

| Band | Type | Frequency range | Default freq | Default gain |
|------|------|-----------------|-------------|-------------|
| 0 | Low shelf | 50–250 Hz | 100 Hz | 0 dB |
| 1 | Low-mid peaking | 300–1500 Hz | 800 Hz | 0 dB |
| 2 | High-mid peaking | 1000–3000 Hz | 2000 Hz | 0 dB |
| 3 | High shelf | 2000–4500 Hz | 3500 Hz | 0 dB |

Gain range: ±6 dB.  Q for peaking bands defaults to 1.0 (stored in prefs for
advanced users but not exposed in the UI).

### Parameters

| Setter | Range | Default | Description |
|--------|-------|---------|-------------|
| `setBandGain(int idx, float dB)` | −6 … +6 | 0 | Gain for band `idx` |
| `setBandFreq(int idx, float hz)` | 10 … Nyquist×0.9 | per-band | Centre/corner frequency |
| `setBandQ(int idx, float q)` | 0.1+ | 1.0 | Q for peaking bands 1, 2 |
| `setShelfSlope(int idx, float s)` | 0.01 … 1.0 | 0.5 | Slope for shelf bands 0, 3 |
| `clearAllBands()` | — | — | Reset all gains to 0 dB |

### Key methods

```cpp
TriplePara eq(48000.0f);          // construct at sample rate
eq.setBandGain(0, +3.0f);        // low shelf +3 dB
eq.setBandFreq(1, 1200.0f);      // low-mid centre → 1.2 kHz
eq.process(inPtr, outPtr, nSamples);
eq.clearAllBands();               // flat response
eq.reset();                       // clear all filter state
```

### Algorithm

Four cascaded biquad IIR filters (Audio EQ Cookbook formulas by Robert
Bristow-Johnson):
- Bands 0, 3: shelving filters (low shelf, high shelf)
- Bands 1, 2: peaking EQ (constant-Q)

Coefficients are recalculated lazily (only when a parameter changes) via a
dirty flag, so repeated `process()` calls with unchanged parameters cost only
the per-sample biquad cascade.

### Porting notes (vs. original LADSPA)

- LADSPA `plugin_data` struct and port pointers replaced by class members.
- `util/biquad.h` functions (`ls_set_params`, `eq_set_params`, `hs_set_params`)
  replaced with Audio EQ Cookbook coefficient calculators inlined directly.
- Reduced from 5 bands (low shelf + 3 peaking + high shelf) to 4 bands
  (low shelf + 2 peaking + high shelf).
- Band frequency ranges tailored for RX audio (50 Hz–4.5 kHz voice range).
- No platform-specific code; compatible with macOS, Windows, and Linux.

### UI — RX Audio Processor settings window

The EQ controls live in the **RX Audio Proc** dialog (`RxAudioProcessingWidget`),
in a "Receive Equalizer" group box between the NR algorithm controls and the
output gain section.  Each band has:
- A **vertical slider** for gain (±6 dB, label on top)
- A **dial (knob)** for frequency (range per band, label below)
- An **Enable** checkbox and a **Clear** button (resets gains to 0 dB)

Q/bandwidth for the two peaking bands is stored in `rxAudioProcessingPrefs::eqQ[]`
(QSettings keys `RxProcEqQ0`–`RxProcEqQ3`) but is not exposed in the UI.
Advanced users can edit the settings file directly to tune Q values.

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

## UI — TX Audio Processor settings window

| Role | File | Class |
|------|------|-------|
| Dialog (UI, slots, meters) | `src/txaudioprocessingwidget.cpp` / `include/txaudioprocessingwidget.h` | `TxAudioProcessingWidget` |
| Compatibility alias | `include/audioprocessingwidget.h` | `using AudioProcessingWidget = TxAudioProcessingWidget` |
| Parameter bridge | `src/wfmain.cpp` — `on_TXaudioProcBtn_clicked()`, `onAudioProcPrefsChanged()`, `applyAudioProcPrefs()` | `wfmain` |
| DSP engine | `src/audio/txaudioprocessor.cpp` / `include/txaudioprocessor.h` | `TxAudioProcessor` |
| Level meter widget | `src/meter.cpp` / `include/meter.h` | `meter` |

### Window lifetime

`TxAudioProcessingWidget` is created lazily when the user first clicks the
**TX Audio Proc** button (`on_TXaudioProcBtn_clicked()`, `src/wfmain.cpp`).
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

## Transmit Audio pipeline — where the plugins are inserted

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

---

## RX Noise Reduction

Two algorithms are provided.  Both are selected from the **RX Audio Proc** dialog
(`on_RXaudioProcBtn_clicked()`).

### SpeexNrProcessor (Speex preprocessor)

**Origin:** `speexdspmini/` — a self-contained copy of the Speex DSP preprocessor
(no system `libspeexdsp` required).

**File:** `src/audio/speexnrprocessor.h` (header-only C++ wrapper)

**Build flags:** `FLOATING_POINT`, `USE_KISS_FFT`, `EXPORT=` (set in `wfview.pro`).

#### Parameters (exposed via RxAudioProcessor setters)

| Setter | Range | Default | Description |
|--------|-------|---------|-------------|
| `setSuppression(int dB)` | −70 … −1 | −30 | Target noise suppression level |
| `setBandsPreset(int p)` | 0 … N−1 | 3 | Number of Speex filter bands (from `filterbank.h`) |
| `setFrameMs(int ms)` | 10, 20 | 20 | Speex processing frame size |
| `setDereverb(bool)` | — | false | Enable dereverberation |
| `setDereverbLevel(float)` | 0 … 1 | 0 | Dereverb strength |
| `setDereverbDecay(float)` | 0 … 1 | 0 | Dereverb tail decay |
| `setAgc(bool)` | — | false | Enable automatic gain control |
| `setAgcLevel(float)` | 1000 … 32000 | 8000 | AGC target level |
| `setAgcMaxGain(int dB)` | 0 … 60 | 30 | AGC max gain |

Band presets are driven at compile time from `filterbank.h` `band_presets[]` and
`FILTERBANK_NUM_PRESETS`, so adding presets to that header automatically extends the combo
box without any other code changes.

### SpacNrProcessor (SPAC — Splicing of AutoCorrelation)

**Origin:** Based on the SPAC algorithm by Jouji Suzuki (1970s), as implemented in the
Kenwood TS-890S speech processor.  Ported to streaming C++ with eight improvements (A–H).

**File:** `src/audio/spacnrprocessor.h` (header-only C++ class; `spac_nr.c` in `src/audio/spac/`
is reference material only and is not compiled into the build).

**Algorithm:** 50 % overlap-add.  Each frame: autocorrelation → pitch detection
(parabolic interpolation, prominence check, continuity tracking) → voiced/unvoiced
classification → cycle tiling with cosine crossfade → soft-blend attenuation.

#### Parameters

| Setter | Range | Default | Description |
|--------|-------|---------|-------------|
| `setFrameMs(float ms)` | 10 … 50 | 20 | Analysis frame size |
| `setVoicingThr(float)` | 0 … 1 | 0.20 | Min correlation score to classify as voiced |
| `setVoicingFull(float)` | 0 … 1 | 0.55 | Score at which blend is 100 % voiced |
| `setAttenDb(float dB)` | 0 … 120 | 80 | Unvoiced attenuation depth |

---

## Receive Audio pipeline

```
Radio UDP RX path  (icomUdpAudio / yaesuUdpAudio / rtpAudio)
    ↓  emit haveAudioData(audioPacket)
audioHandlerBase (output / playback handler)
    ↓  slot incomingAudio(audioPacket)
audioConverter  (TimeCriticalPriority thread)
    src/audio/audioconverter.cpp
    1. Decode (Opus / PCM / ADPCM / uLaw)
    2. ── processingHook ──────────────────────────────────────────
          RxAudioProcessor::processAudio(samples, sampleRate, channels)
              a. Snapshot parameters (QMutex, brief)
              b. emit rxInputLevel → Input level display
              c. Apply noise reduction (SpeexNrProcessor or AnrNrProcessor)
                 on selected channel(s) per channelSelect
              d. Apply EQ (TriplePara, 4-band parametric) if enabled
              e. Apply output gain, clip to [−1, 1]
              f. Mix sidetone (AFTER NR so user's own voice is untouched)
              g. emit rxOutputLevel → Output level display
    3. Resample to native audio device rate
    4. Write to QAudioSink / PortAudio / RtAudio output device
```

### Sidetone intercept point

When `RxAudioProcessor` is active (LAN connection only), the sidetone signal from
`TxAudioProcessor::haveSidetoneFloat` is routed to
`RxAudioProcessor::injectSidetone` **instead of** the UDP audio class's own
`injectSidetone`.  This ensures the user hears their own voice **after** noise
reduction, not before.

Connection sites: `icomudpaudio.cpp`, `yaesuudpaudio.cpp`, `kenwoodcommander.cpp`.
The conditional branch checks `rxSetup.rxProc != nullptr`.

### Where the hook is installed

`audioHandlerBase::init()` (`src/audio/audiohandlerbase.cpp`) installs the hook via
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` when
`!setup.isinput && setup.rxProc` is non-null.

### UI

| Role | File | Class |
|------|------|-------|
| Dialog | `src/rxaudioprocessingwidget.cpp` / `include/rxaudioprocessingwidget.h` | `RxAudioProcessingWidget` |
| Parameter bridge | `src/wfmain.cpp` — `on_RXaudioProcBtn_clicked()`, `onRxAudioProcPrefsChanged()`, `applyRxAudioProcPrefs()` | `wfmain` |
| DSP engine | `src/audio/rxaudioprocessor.cpp` / `include/rxaudioprocessor.h` | `RxAudioProcessor` |
| NR back-ends | `src/audio/speexnrprocessor.h`, `src/audio/anrnrprocessor.h` | `SpeexNrProcessor`, `AnrNrProcessor` |
| RX EQ | `src/audio/plugins/triple_para.cpp` / `triple_para.h` | `TriplePara` |

The **RX Audio Proc** button is enabled only for LAN connections (`prefs.enableLAN`).
`RxAudioProcessor` is created once when the first radio connection is set up and
reused across reconnects.

---

## Spectrum Display (TX and RX)

Both the TX and RX audio processing dialogs include an optional spectrum display
powered by `SpectrumWidget` (`include/spectrumwidget.h`, inline Q_OBJECT class;
`src/audio/spectrumwidget.cpp` exists only to wire the moc).

### FFT back-end

The spectrum FFT uses **pocketfft** (`src/audio/pocketfft/pocketfft.h`,
`src/audio/pocketfft/pocketfft.c`), a small self-contained real-FFT library
compiled directly into the build — no external dependency.  Plans are created
once in the constructor (`make_rfft_plan(SPEC_FFT_LEN)`) and destroyed in the
destructor (`destroy_rfft_plan()`).

### Algorithm

Both `TxAudioProcessor::appendSpectrumSamples()` and
`RxAudioProcessor::appendSpectrumSamples()` use the same algorithm:

1. **Decimation** — Input samples are decimated to ~16 kHz effective rate
   (`K = round(sampleRate / 16000)`, minimum 1).  Every K-th sample is written
   into a pair of circular ring buffers (`m_specInRing` for pre-DSP,
   `m_specOutRing` for post-DSP), each of length `SPEC_FFT_LEN` (1024).

2. **Trigger** — After every `triggerEvery` decimated samples an FFT is fired.
   `triggerEvery = round(decimatedSR / targetFps)`.  At 48 kHz with K=3 and
   30 fps this is ~533 decimated samples, yielding ~3 FFTs per audio block.

3. **Windowing** — The ring buffer contents are copied into a work buffer and
   multiplied by a **Hanning window** (precomputed on sample-rate change):
   ```
   w[i] = 0.5 − 0.5 × cos(2π × i / (N − 1))
   ```

4. **FFT** — `rfft_forward(plan, buf, 1.0)` computes a real-input FFT in-place.
   The output uses pocketfft's half-complex layout:
   `buf[0]` = DC, `buf[2k−1]`/`buf[2k]` = re/im of bin k (k = 1 .. N/2 − 1).

5. **Magnitude (dBFS)** — Each bin is converted to dBFS:
   ```
   dBFS[k] = 20 × log10( |bin[k]| / (N/4) + 1e-10 )
   ```
   The normalisation constant `N/4` accounts for the Hanning window coherent
   gain (0.5): a full-scale sine at bin k produces `|bin[k]| = N/2 × 0.5 = N/4`,
   so the formula yields 0 dBFS.

6. **Emission** — Both in and out bin vectors (`SPEC_FFT_LEN / 2` = 512 values)
   are emitted as `QVector<double>` via a queued signal
   (`txSpectrumBins` / `rxSpectrumBins`).  Qt deep-copies the vectors into the
   event payload, so the converter thread can immediately reuse its buffers.

### Capture points

| Path | Input capture | Output capture |
|------|---------------|----------------|
| TX (normal) | Raw microphone signal **before** noise gate and input gain | After output gain + clip (final processed audio) |
| TX (bypass) | Raw signal | Same raw signal (input == output) |
| RX (normal) | Raw radio audio **before** noise reduction | After output gain + clip + sidetone mix |

### Display

`SpectrumWidget` repaints on a `QTimer` at a configurable frame rate (default
10 fps RX, 30 fps TX).  It draws two traces on a logarithmic frequency axis
(50 Hz – 8 kHz, each octave equal width) over a −90 to 0 dBFS vertical range:

- **Input** (green, `spectrumPrimary`) — drawn first (behind)
- **Output** (orange α 180, `spectrumSecondary`) — drawn on top

Bin-to-frequency mapping: `freq = k × (effectiveSR / SPEC_FFT_LEN)`, where
`effectiveSR = sampleRate / K`.

### Files

| File | Description |
|------|-------------|
| `src/audio/pocketfft/pocketfft.h` | pocketfft C API header |
| `src/audio/pocketfft/pocketfft.c` | pocketfft implementation |
| `include/spectrumwidget.h` | `SpectrumWidget` inline QWidget (Q_OBJECT, paints spectrum) |
| `src/audio/spectrumwidget.cpp` | Translation unit for moc wiring |
| `src/audio/txaudioprocessor.cpp` | `appendSpectrumSamples()` — TX spectrum FFT + decimation |
| `src/audio/rxaudioprocessor.cpp` | `appendSpectrumSamples()` — RX spectrum FFT + decimation |
| `src/txaudioprocessingwidget.cpp` | `onSpectrumBins()` slot, feeds SpectrumWidget |
| `src/rxaudioprocessingwidget.cpp` | `onSpectrumBins()` slot, feeds SpectrumWidget |

### Preferences

| QSettings key | Struct field | Default | Description |
|---------------|-------------|---------|-------------|
| `TxProcSpectrumEnabled` | `audioProcessingPrefs::spectrumEnabled` | false | Enable TX spectrum |
| `TxProcSpectrumFps` | `audioProcessingPrefs::spectrumFPS` | 30 | TX spectrum target fps |
| `TxProcSpecInhibitDuringRx` | `audioProcessingPrefs::specInhibitDuringRx` | true | Pause TX spectrum while receiving |
| `RxProcSpectrumEnabled` | `rxAudioProcessingPrefs::spectrumEnabled` | false | Enable RX spectrum |
| `RxProcSpectrumFps` | `rxAudioProcessingPrefs::spectrumFPS` | 10 | RX spectrum target fps |

