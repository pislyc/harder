# HARDER — verification report (1.2.0: standalone program + VST3 plugin)

All numbers below were measured in the build environment (Linux x86-64 VM, 2 cores; Windows binaries run under Wine 9).

## C. New in 1.2.0: SAMPLE machine and the user library

`tools/sample_test.cpp` (139 checks), built natively with AddressSanitizer + UBSan and with MinGW for Windows (run
under Wine): all pass on both.

| check | result |
|---|---|
| WAV decoding: 8/16/24/32-bit PCM, float 32/64, extensible, stereo, odd chunk padding; garbage, MP3-in-WAV refused; 61 s file | values within the source's quantization, mono mix exact; stored form and `encodeWav` round trip bit-exact; cut at 60 s |
| SAMPLE playback vs a direct computation (DC blocker included): unity, +12 st, -12 st (Hermite), 44.1 kHz file at 48 kHz, reverse, loop, START/END with the 32/64-sample fades, LEV 50 | all within 1e-6 (unity, +12 st, reverse: the sample values exactly) |
| empty slot / end of sample | silent, voice released |
| project with samples in 3 formats (s16 / s24 / f32) | save -> load -> save identical; ids and data bit-exact; without samples the project file has no `samples` key (web-compatible, `core_test` web round trip unchanged); undo snapshots carry ids only (29 KB instead of 51 KB for the test project; the difference grows with the samples) and resolve from the session cache |
| library: save SAMPLE / other sounds, Chinese names, INBOX, the sound's own WAV hidden, move + rename (WAV follows, reference rewritten), trash, trash folder, grooves, a sound file written by another program | all as specified in docs/LIBRARY.md; the documented Python example produces a sound HARDER loads |
| sample lifetime: audio thread draining + processing while the main thread replaces samples 6000 times, forces queue overflows, resends | no use-after-free under ASan in 10 runs. The test is sensitive: freeing retired samples at once, or dropping the overflow protection, makes ASan report a heap-use-after-free in `MSmp::ctl` (1 of 3 runs for the overflow case) |
| real binaries in the VST3 host test (Linux .so, Windows DLL under Wine): project with two SAMPLE tracks (incl. a 20 s float sample: 5 MB state) through set_state / get_state, notes on C1 and C#1 | samples returned bit-exact, state string identical; output vs reference engine max diff 0 |
| Windows exe under Wine, real mouse and keyboard: IMPORT WAV (open dialog), + NEW category, drag an item onto a category, SAVE SOUND (JSON + WAV pair), Chinese category / file names written from outside (rendered with a CJK fallback font), auto-load of a WAV renamed into INBOX, groove SAVE PATTERN / drag / RENAME / DELETE (two presses, to Library/Trash) / category rename by double-click, CLEAN UP SLOTS | all as intended; auto-load picked the new file within 1.5 s; screenshots in the release notes |
| Windows exe: two SAMPLE tracks + six synth tracks played (Space), recorded from the audio device, compared with the engine rendering the autosaved project | from a cold start: identical (difference 0) until 2.5 s. Then single hits differ by exactly one sample: events whose exact time falls on an integer sample (step 22 at 132 BPM = 120000.000, step 29 + swing = 158400.000) round either way depending on the engine time at PLAY (`tools` origin scan: the same project fires such a hit at 38400 or 38401 depending on that time). This is the sequencer's timebase (the same code as the web engine), not the SAMPLE machine. After auditions, a -115 dB residue of earlier sounds in the FX buffers is added for 0.5 s. |
| autosave restore | restarting the exe brings back both samples, the slots and the library state |
| CPU: 8 looping, pitched, filtered SAMPLE voices with 1/32 retrigs on every step, 64-sample blocks | 3 % of a core on average; p99.9 110-220 us, worst 209-257 us of the 1333 us budget in clean runs (rare outliers are VM scheduling, as for the synth stress test) |
| engine equivalence with the web app (36 scenarios, 44.1/48/96 kHz), presets, preview, export, generator, host echo, sync | unchanged: max 5.96e-8, 0/111, 0/222, kit byte-identical / loop 201 of 39.9M samples 1 LSB, 0 mismatches, 0/111, drift 0 |

Found and fixed while building it: the command queue counted dropped commands, so after an overflow retired samples
were never freed, and a sample replaced by a dropped command could have been freed while the engine still played it.
Not verified: typing Chinese into the name field (Wine does not pass xdotool's Unicode keys); on Windows the field
takes WM_CHAR text, in a host the plugin gets only ASCII (DPF's VST3 key forwarding).

## B. New in 1.1.1: level strip

Output level (OUT fader): a gain after the limiter, smoothed over ~15 ms, stored in the UI state (not in the project,
not in exports). Selected-track fader: the track's VOL parameter (project, undo, host automation). M / S: mute and
solo of the selected track.

| check | result |
|---|---|
| real binary in the VST3 host test, output level set through the UI state, MIDI note played: Linux and Windows DLL | -12 dB: output = reference x 0.2512 within 1.7e-9; -inf: exact silence; 0 dB: bit-identical to before |
| Windows exe under Wine: OUT reset by double-click, 24 wheel steps down, then Space, recorded from the audio device | `outDb` -12 saved; recording = engine render x 0.2512 in 138 of 140 1000-frame chunks (max 1.1e-8); the 2 others hold a one-sample insertion of Wine's capture clock |
| fader interactions in the standalone | drag to the left end -> off (-inf), double-click -> 0 dB, drag -> -10.3 dB, 2 wheel steps -> -9.3 dB, track fader drag -> -5.8 dB |
| M and S on track 1 | track 1 muted and soloed, other tracks drawn dimmed, markers M S on the track button |

### Crash found while testing 1.1.1, fixed

While testing, one start of the Windows exe crashed: a page fault in RtAudio's WASAPI stream thread (address mapped
with a non-stripped rebuild of the same source: `RtApiWasapi::wasapiThread`, the write to `stream_.apiHandle`), then
"terminate called after throwing RtAudioError: Unable to initialize render audio client". Cause: RtAudio's
`stopStream` waits only two buffer lengths for the stream thread; a thread still initialising the device keeps
running while the stream is freed. HARDER reopens the stream right after start when the saved audio settings differ
from the defaults, and on every settings change, so this could happen on real Windows too. Second issue: without an
error callback RtAudio throws from its own thread on a driver error (device removed), which ends the program.

Fix: `DPF_RTAUDIO_WASAPI_JOIN_THREAD` (stop and abort wait for the stream thread itself, up to 5 s); the bridge passes
an error callback (a driver error marks the output as stopped instead of ending the program); the standalone then
shows NO AUDIO OUTPUT and retries the chosen device, then the system default, every 3 s.

Evidence: a direct RtAudio test (`open -> start -> stop -> close` at once, 300 times, Windows build under Wine):

| RtAudio | 4 runs of 300 cycles |
|---|---|
| unpatched | 2 crashed (terminate: Unable to initialize render audio client), 2 hung (300 s timeout) |
| patched | 4 of 4 completed, no driver error |

The UI path did not reproduce the crash in 20 normal starts, 20 starts under full CPU load, and 50 rapid buffer
changes, for the old and the new exe alike; the race window is narrow under Wine. The retry every 3 s was observed
in the log; an actual reconnection could not be shown under Wine, which enumerates audio devices once per process.

## A. New in 1.1.0: standalone program and export

The standalone `HARDER.exe` is the same plugin code in DPF's standalone wrapper (JACK if a JACK server runs, else the
Windows audio driver through RtAudio/WASAPI and RtMidi/WinMM), with these additions, each a marked patch in `dpf/`:
audio device / buffer / MIDI-input selection at run time, sample-rate changes passed to the engine, start without a
stream when no device opens (`DPF_NATIVE_AUDIO_SETTINGS`); no Media Foundation, streams at the device's mix rate
(`DPF_RTAUDIO_WASAPI_NO_MF`, so the program also starts on Windows "N" editions); save dialogs pre-filled and asking
before replacing a file (`DPF_WIN_SAVE_DEFAULT_NAME`).

Tested on the Windows exe running under Wine with a capturable audio device (ALSA file device behind Wine's WASAPI):

| check | result |
|---|---|
| program depends only on Windows system DLLs (GDI32, KERNEL32, OPENGL32, SHELL32, USER32, WINMM, comdlg32, dwmapi, msvcrt, ole32); GUI subsystem, own icon | yes |
| Space pressed in the window (X keyboard event -> Wine -> scan code), pattern recorded from the audio device, compared with the engine rendering the same project offline | identical to 5.96e-8 for 2.46 s (118 000 frames), until Wine's clock correction inserted one sample into the capture |
| buffer 512 -> 256 and a device chosen in LAB > AUDIO | stream reopened, audio continues, `settings.json` written, reapplied after restart |
| French AZERTY layout (setxkbmap fr): keys printed "a", "&", Shift+"é" | steps 9 and 1 toggled, step 2 trigless lock: physical positions as documented |
| L, arrow keys | drawer closed, track and page changed |
| close the window, start again | project (incl. a step toggled 0.2 s before closing), UI state and audio settings restored |
| no usable audio device at start | program opens, header "NO AUDIO OUTPUT", project restored (DPF 1.0 standalone exits with an error box) |
| EXPORT LOOP / EXPORT KIT through the Windows save dialog (name pre-filled) | zips byte-identical to the same project exported by the Linux build |

Export vs the web app (`tools/export_ref.js` runs the web app's own export functions, cut out of `ui.js`, on 10
projects: default, 8 material kits with generated grooves incl. mutes and half-time, a polymeter pattern; 1 and 2
loops, with stems): 90 kit one-shot WAVs byte-identical; loop WAVs identical except 201 of 39.9 M samples that differ by
one 24-bit step (the engines differ by <= 6e-8, which crosses a rounding boundary that rarely); MIDI files carry the same
events; only the order of simultaneous notes differs (the web sort leaves it to the JavaScript engine; note-offs precede
note-ons in both); project JSON equal.

Not tested: MIDI input in the standalone (no MIDI device or ALSA sequencer in the build environment; the port selection
code calls RtMidi as DPF does); real Windows audio drivers and their latency.

## 0. Fixed in 1.0.1: sound library under a host

Symptom (reported from Ableton Live): many library sounds sounded the same, others very different from the web app.

Cause. The engine was not at fault (it renders every sound like the web app, see 1). The fault was in how the
plugin took parameter values from the host. Loading a sound changes up to 18 automatable parameters per track
(SRC 1-8, filter, decay, volume, sends...), and the plugin tells the host about each change. Hosts can send old
values back while that is in flight; Ableton Live is reported to send the processor the value it last knew when an
edit gesture begins (forum.juce.com/t/60565). 1.0.0 took every host value as automation and wrote it into the
engine and the project, so the new sound was overwritten with the previous sound's values: same engine ->
"sounds the same", other engine -> the old values mapped onto the new engine -> "very different". In addition,
DPF applied only the *first* of several values queued at the same sample offset, i.e. the stale one.

Fix.
- The plugin owns a value it has just set (sound/kit/project loaded, knob turned): for 1 s a host value that
  disagrees is refused and the plugin's value is reported back to the host; echoes of the current value change
  nothing. After that host automation applies as before.
- DPF patch (`DPF_PARAM_REFUSAL`): the last value at an offset wins; a refused value is not kept in the wrapper's
  cache (what the host displays and saves) and is sent back to the host; after a state load the cache is read
  back from the plugin.
- The OLED waveform preview reused a voice without clearing its sample-rate-reduction/bit/filter/comb/LFO state,
  so DIGITAL sounds were drawn wrong (up to 1.09 off). The voice is now reset to exactly the web's initial state.

Evidence, same test before and after (`tools/vst3_host_test.cpp`: a minimal VST3 host that loads the real binary,
loads sounds, sends the old values back the way Live does, plays a MIDI note, and compares the audio sample by
sample with an engine fed the web app's messages):

| binary | sounds that differ from the web engine | reverted to the previous sound | host shows wrong values | automation, last of two points |
|---|---|---|---|---|
| 1.0.0 Windows DLL (under Wine) | 11 of 12, up to 1.0 | 11 of 12 | 12 of 12 | lost (20 instead of 45) |
| 1.0.1 Windows DLL (under Wine) | 0 of 12 (diff 0) | 0 | 0 | 45 |
| 1.0.1 Linux | 0 of 12 (diff 0) | 0 | 0 | 45 |

All 111 sounds through the UI code path, the command queue and a model of DPF + Live (`tools/host_echo_test.cpp`):
1.0.0 code: 111 of 111 reverted; 1.0.1: 0 of 111 differ. Without host echo (`tools/preset_path_test.cpp`):
0 of 111. Waveform preview vs the web's renderOneShot (`tools/preview_test.cpp`): 0 of 222 renders differ
(worst 3.6e-12, was 1.09). Real UI clicks in the standalone build, recorded from its audio output: ELECTRIC and
DIGITAL libraries, every sound's peak and RMS equal to the expected render.

## 1. Sound: the plugin engine equals the web engine

12 scenarios rendered by the web engine (JS, unmodified `engine.js`) and by the C++ engine, same messages at the same
sample positions, compared sample by sample. Scenarios: default project with pattern cues, fill, mutes and master
changes; each of the 8 material kits with two generated grooves (incl. half-time); a polymeter pattern with heavy
locks, retrigs and conditions; all 111 presets as one-shots; an automation storm with rolls, solo, LFOs and tempo/swing
changes.

| sample rate | worst max abs difference | worst error vs signal |
|---|---|---|
| 44.1 kHz | 2.98e-8 | -167.7 dB |
| 48 kHz | 5.96e-8 | -168.3 dB |
| 96 kHz | 5.96e-8 | -161.3 dB |
| 48 kHz, Windows build (MinGW, under Wine) | 2.98e-8 (subset of 7) | -186.6 dB |

5.96e-8 is one step of a 24-bit/float output: the renders are identical apart from the last bit of rounding.
One real difference was found and fixed on the way (single-hit retrigs must still apply RFAD).

The groove generator, kits and presets were ported too and compared against the web code: 1001 generated objects
(default project, every style x kit x option set, mutations, polymeters), 694 715 values, 0 mismatches. A
`harder-project.json` saved by the web app loads into the plugin unchanged.

## 2. CPU

Thread CPU time of the engine (excludes OS preemption), 64-sample blocks. Measured on the build VM before 1.0.1; 1.1.0 was
rerun on a different (rebuilt) VM where all cases are 1-2x slower for the old and the new engine alike (A/B run of
both builds on that VM: worst case at 96 kHz 13-15 % for each), so the engine changes in 1.1.0 cost nothing measurable.
On that VM single 64-sample blocks at 96 kHz occasionally exceeded their 667 us budget, for both builds: that is VM
scheduling jitter; on a real machine use a buffer of 128 or more at 96 kHz.

| case | 48 kHz avg | 48 kHz p99 block | 96 kHz avg |
|---|---|---|---|
| default project playing | 2.2–2.9 % of one core | 64–86 us of 1333 | 4.3–5.0 % |
| stopped, tails finished (idle) | 0.07–0.10 % | 4–16 us | 0.15–0.18 % |
| worst case: 8 heavy voices, 1/64 retrigs over 16 steps, 300 BPM, 2x speed, all FX, slides | 7–8 % | 280–300 us | 13–15 % |
| worst case + 40 parameter changes every 32 samples | 7–8 % | 160–210 us of 667 | 12–13 % |
| reverb size/decay automated every block | 2.4 % | 67 us | 4.7 % |
| clock started past 2^32 samples (25 h uptime) | 2.5–2.8 % | 124–153 us of 2667 (128 blocks) | 4–4.6 % |

What keeps it there:
- no heap allocation on the audio thread (checked with an allocation counter during a 6 s two-thread run: 0)
- no locks on the audio thread: UI commands go through a try-lock queue; at most 384 commands and 2 pattern copies
  are applied per block, the rest waits for the next block
- FTZ/DAZ set for every block (denormal arithmetic is ~30x slower without it)
- FX chain sleeps when no voice sounds and delay/reverb tails are below -140 dBFS for a full delay length
- master FX recomputed at most once per block however many automation points arrive
- private copy of musl's libm for sin/cos/tan/exp/pow/log10: under Wine's msvcrt the C-runtime functions were
  4–10x slower; with the private copy the Windows build runs at native speed (2.8 % vs 4.9 % for the same render)
- 64-bit counters (the web limiter index would wrap after ~12 h)
- the DSP is built without -ffast-math so NaN guards stay in; a voice or the FX chain that produces NaN/Inf is reset

Host sync: 60 s of tempo automation between 80 and 160 BPM with loop jumps: the sequencer stays on Live's grid
(residual drift 0.00 samples after each block's correction). Starting Live at bars 1, 2, 4, 8 and 13, on the beat and
0.37 beats after it, with a 13-step polymeter track and tracks at 1/4x and 3/2x speed: the trigs (track, time,
velocity, conditional trigs included) are the same as when the song plays through from the start, within 1 sample.

## 3. Plugin validation (1.2.0 binaries)

- pluginval, strictness 10, Linux build, full suite including editor and fuzz tests: SUCCESS
- pluginval, strictness 10, Windows build under Wine, editor tests skipped (under Wine the editor test times out for
  every plugin, including DPF's own examples): SUCCESS, including "Fuzz parameters"
- minimal VST3 host test (`tools/vst3_host_test.cpp`) on both binaries: PASSED (see 0)
- Windows binary depends only on Windows system DLLs (static libstdc++/libgcc/winpthread)
- Note for reproducing: pluginval needs an X display (Xvfb) on Linux and for Wine; without one its own JUCE editor
  code crashes (Linux) or the scan times out (Wine), for 1.0.0 and 1.0.1 alike.

## 4. Not verified here

- Running inside Ableton Live or directly on real Windows (no Windows machine in the build environment): the Windows
  UI was seen on screen only under Wine. Live's handling of parameter values is modelled from published reports
  (stale value at gesture start), not observed in Live itself; the fix does not depend on the exact timing as long
  as Live's stale values arrive within 1 s of the plugin's own change. The Linux build of the same UI code was exercised under X11.
- CPU figures on your machine will differ; the DSP readout in the plugin header shows the real load.
