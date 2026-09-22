# Delay - CLAP Delay Effect

A stereo CLAP delay effect with built-in real-time instrumentation, used as the
measurement artifact for the thesis *Real-Time Audio Effect Processing: CLAP
Plugin Standard for Low-Latency Applications*.

## Implemented features

- CLAP plugin entry point and plugin factory
- Stereo audio input and output ports
- Real-time circular delay buffer (power-of-two sized, bitmask wrapping)
- Sample-accurate event handling via block splitting
- Denormal protection (FTZ/DAZ) around `process()`
- Automatable parameters:
  - Bypass
  - Delay Time, 1 ms to 2000 ms
  - Feedback, 0 % to 95 %
  - Mix, 0 % to 100 %
  - Output, -24 dB to +12 dB
  - DSP Complexity, 0 % to 100 % (artificial, calibrated processing load)
  - Run Marker, 0 to 999 (measurement control, see below)
- CLAP state save/load so the host can restore plugin settings in a project

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The build type defaults to `Release` if none is given. **Never measure a Debug
build**: without optimisation the inner loop is dominated by unelided atomic
loads and un-inlined calls, so the timings characterise the compiler rather
than the DSP. The build type is recorded in every CSV file, so a stray Debug
measurement is detectable after the fact.

Optional, for the inner-loop comparison measurement only:

```bash
cmake -S . -B build-naive -DCMAKE_BUILD_TYPE=Release \
      -DELAY_NAIVE_INNER_LOOP=ON
```

This rebuilds the DSP coefficients for every sample (including a `std::pow()`
call for the output gain and six atomic parameter loads) instead of once per
event segment. Comparing the two builds at DSP Complexity 0 quantifies the cost
of naive parameter handling in a real-time inner loop.

## Install in REAPER

Copy `Delay.clap` to a CLAP plugin directory and rescan plugins.

```text
C:\Program Files\Common Files\CLAP\
%LOCALAPPDATA%\Programs\Common\CLAP\
```

Then in REAPER: `Options -> Preferences -> Plug-ins -> CLAP -> Re-scan`, and
search for `Delay` in the FX browser.

## Measurement instrumentation

### What is measured

`ScopedProcessTimer` measures the wall-clock duration of every `process()` call
using `std::chrono::steady_clock`, and hands it to `RealtimeMetrics`. Each block
is placed into a fixed log-spaced histogram (512 bins, 10 ns to 1 s, 64 bins per
decade). No allocation, locking or I/O happens on the audio thread; the
histogram is a plain array of relaxed atomic counters.

A block counts as an **overload** when its processing time exceeds the nominal
block duration `block_size / sample_rate`. Note that this measures whether the
plugin would meet a hard deadline, which is not the same as whether the host
produced an audible dropout — see "Host configuration" below.

The first **2 seconds of audio** are discarded as warm-up before statistics
begin. The window is expressed in frames, so it covers the same amount of audio
at every block size. Without it, cold caches and page faults on the first blocks
permanently contaminate the maximum.

### Run control

The **Run Marker** parameter controls measurement runs. Changing its value
discards all collected statistics and starts a new run immediately, without
deactivating the plugin. The value appears in the output filenames.

A run therefore looks like this:

1. Set sample rate and buffer size in the interface control panel.
2. Set DSP Complexity and any other parameters. **Set them before starting the
   transport**, never during — a mid-run change contaminates the statistics.
3. Increment Run Marker (1, 2, 3, ... for repeated runs at one configuration).
4. Play the test file from the start.
5. Stop. The CSV files are already complete on disk.

### Output files

Files are written to `%TEMP%\DelayMetrics\` by default. Override with the
`DELAY_METRICS_DIR` environment variable.

Three files per run, all rewritten on every poll (250 ms) so they can be
collected at any time after the transport stops:

| File | Contents |
| --- | --- |
| `*_summary.csv` | One row with the final result of the run |
| `*_timeline.csv` | One row per poll, showing how the statistics converge |
| `*_histogram.csv` | The full per-block distribution, one row per populated bin |

Filenames follow
`Delay_sr<rate>_buf<block>_run<marker>_<timestamp>_<kind>.csv`.

Every file carries sample rate, block size, complexity, delay parameters, build
type and inner-loop mode as columns, so a file remains self-describing even if
it is moved or renamed.

The summary is deliberately a **one-row CSV**: concatenating the summary files
of an entire measurement campaign yields a single table ready for the evaluation
chapter.

```bash
python tools/aggregate_runs.py "%TEMP%\DelayMetrics" -o results.csv
```

### Reported statistics

| Column | Meaning |
| --- | --- |
| `min_ms`, `mean_ms`, `max_ms` | Exact, over the measured window |
| `p50_ms`, `p90_ms`, `p99_ms`, `p999_ms` | From the histogram |
| `mean_load_percent`, `p99_load_percent`, `max_load_percent` | Processing time as a share of the block deadline |
| `overload_count`, `overload_rate_percent` | Blocks exceeding the deadline |
| `histogram_underflow`, `histogram_overflow` | Blocks outside the histogram range; should be 0 |

Percentiles are reported as the upper edge of the containing bin, capped at the
exactly tracked maximum. Bin width bounds their resolution at about 3.6 %.
Report worst-case and high percentiles rather than the mean: for a hard
real-time deadline the mean is not the quantity of interest.

## Host configuration for measurements

- **Disable anticipative FX processing** in REAPER
  (`Preferences -> Audio -> Buffering`, and the per-track performance options).
  With it enabled, track FX are rendered ahead of the play cursor on worker
  threads, so the plugin is not running against a hard deadline and the
  overload counter no longer corresponds to audible failure.
- **Set the Windows power plan to High Performance** and disable turbo if
  possible. CPU frequency scaling is the most likely source of otherwise
  unexplained variance between blocks at low load.
- Close other applications and pause background scanners.
- Confirm the sample format actually in use: REAPER's 64-bit processing option
  selects the `double` path. The format is recorded as `audio_format_bits`.

## Known real-time caveats

`DelayProcessor::reset()` clears the entire delay line, which is O(n) work on
the audio thread. CLAP specifies `reset()` as `[audio-thread & active]` and
requires all buffers to be cleared, so this is a documented exception mandated
by the specification itself. It lies outside the timed `process()` path and does
not affect the reported measurements.

The delay reads at integer sample positions; there is no fractional-delay
interpolation and no parameter smoothing, so delay-time changes step
discontinuously. This is out of scope for a latency study but should be stated
explicitly rather than implied otherwise.
