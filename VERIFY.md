# HARDER — verification report (2.0.1: standalone program + VST3 plugin; macOS standalone)

All numbers below were measured in the build environment (Linux x86-64 VM, 2 cores; Windows binaries run under Wine 9),
except section G's GitHub Actions part, which runs on a GitHub macOS runner.

## G. New in 2.0.1: macOS standalone (HARDER.app, Universal arm64 + x86_64)

| check | result |
|---|---|
| engine on AArch64 (the Apple Silicon instruction set), before any Mac: the tools built with clang 18 for aarch64-linux, run under qemu-aarch64, flush-to-zero through FPCR.FZ (as the app does) | 36 equivalence renders (12 scenarios x 44.1 / 48 / 96 kHz) **bit-identical** to the Linux x86-64 renders (SHA-256, `tools/equiv.sh`), so also 5.96e-8 from the web engine as before; `v2_test` 74/74 (detection 11/12 at 50, 12/12 at 85, same as x86), `sample_test` 155/155, `control_test` 61 809/61 809 |
| engine with clang on x86-64 (the Intel slice's compiler family) | 36 renders bit-identical to the gcc build |
| the private libm everywhere | transient detection used the C runtime's `log10`; now `hm_log10` like the engine (slice points cannot depend on the platform). v2/sample tests unchanged under ASan/UBSan |
| Linux and Windows after the changes | Linux VST3 host test passed (multi-out, memory, automation, samples unchanged); Windows exe self-test under Wine: WASAPI open, Space by scan code started the sequencer, report written, quit with autosave; Linux standalone self-test under JACK: same |
| GitHub Actions (`.github/workflows/macos.yml`, macos-15 arm64 runner) | builds the Universal app with Xcode, checks the archs / linked libraries / signature / Info.plist, runs `tools/equiv.sh` against the same SHA-256 list natively on arm64 and under Rosetta (x86_64), the 8 test suites on arm64, and starts the app on both architectures in self-test mode (Space by its macOS key code -> sequencer runs, JSON report, Quit through `NSApp terminate:` -> exit 0 with autosave.json written, no crash report), with a screenshot. Run of 5e6199f (macOS 15.7.9 arm64, Xcode 16.4): build OK (one ranlib note on the fat archive), x86_64 + arm64, system libraries only, signature valid, 5.1 MB; equivalence 36/36 bit-identical on arm64 and on x86_64 under Rosetta; all 8 suites passed on arm64 (v2 74/74, sample 155/155, control 61 809, sync, preset path 0/111, host echo 0/111, core: 0 audio-thread allocations, fuzz 36 000 no crash); app arm64 and x86_64: CoreAudio open (the VM's virtual device, 48 kHz / 512), Space -> sequencer running, layout key name "Q", CJK font STHeiti, frame read back from "Apple Software Renderer" 1180x640 with 153 / 154 distinct colours (the UI is drawn; the runner's screenshot shows the window empty because the software-rendered GL surface is not captured), exit code 0, autosave.json written on quit, no crash report. First run: failed only because DPF does not create ~/Documents (fresh HOME in CI); fixed in harderConfigDir() |
| not verified yet | a real Mac with its GPU (the runner renders in software): sound from the speakers, a MIDI keyboard / IAC clock, the file dialogs, Retina scaling, Gatekeeper's first-start prompt. The macOS-only code (key-code table, layout key names, menu and Quit, Finder, CJK fonts) is only compiled and exercised on the CI runner |

## F. New in 2.0.0: user kits, chain / song, slices, stereo, resampling, MIDI clock, multi-out

| check | result |
|---|---|
| existing projects | the 36 web-equivalence renders (44.1 / 48 / 96 kHz) are bit-identical to 1.3.1 (vs the web engine: max 5.96e-8 as before); generator 1001 objects 0 mismatches; presets through the UI path 0/111; host echo 0/111; preview 0/222; sync drift 0; `core_test` passed (0 heap allocations on the audio thread); the 20 export zips of the 10 test projects are byte-identical to 1.3.1. A project that uses none of the 2.0 features is written exactly as before |
| `tools/v2_test.cpp`, native with ASan/UBSan and as a Windows exe under Wine | 74 checks pass on both (below) |
| slices | equal slices (8/16/32/64) and slice points (AUTO, SLCE = step or a fixed slice): playback range = the slice; a one-shot slice plays the sample's values exactly (fades only where the slice cuts); a reversed slice starts at its last frame; in the sequencer each of 16 trigs starts at its own slice |
| stereo samples | both channels played, separately filtered; a mono sample without slices keeps its 1.x id (old projects and undo snapshots still resolve) |
| chain / song (internal clock) | chain order and times exact; song rows with repeats, row mutes (no trig of a muted track in that row, also for steps scheduled before the row starts), a row tempo of 150 BPM (step spacing within 1 sample), STOP at the end; a pattern cued during the song plays once, then the song continues where it was |
| song under a host | starting the host at 6 positions (beats 0, 4, 8, 9.25, 17, 26, inside rows and repeats) gives the same trigs as playing from bar 1 (within 1 sample); with STOP, a host position after the end is silent |
| resampling | MAIN from the pattern start = the output sample for sample; a track tap = that track after pan and volume (< 1e-6); stopping keeps what was recorded; cancelling while waiting records nothing |
| track outputs (engine) | main output without the routed track = main output with the track muted (difference 0); the routed output = the track at the main mix's level, delayed by the limiter's look-ahead (< 1e-6) |
| MIDI clock (component) | ±2 ms jittered clock at 125 BPM: same trigs as the internal clock started on the first tick, within 1.5 ms, tempo within 0.05 BPM; Stop; Continue at song position 32 resumes at beat 8; clock out: one Start just before the first step, ticks every quarter / 24 exactly, Stop |
| kits | save (incl. a stereo sample with cue slices and a Chinese track name) and load: machines, parameters, samples (same ids), roles, master FX; rename moves the samples folder and rewrites the references; trash takes the samples along; EXPORT KIT renders SAMPLE tracks (they were silent before 2.0, fixed) |
| WAV | stereo WAV with cue points: round trip bit-exact, slice points = cue points |
| transient detection | 12 noise hits of 0.2-0.9 on each other's tails: sensitivity 85 finds 12 of 12, sensitivity 50 finds 11 (the 12th rises 3.9 dB over a louder hit's tail), both with no false points, all within 1.3 ms of the onset |
| VST3 host test, real binaries (Linux .so, Windows DLL under Wine) | 9 output buses (main bus active by default, "Track 1"-"Track 8" auxiliary and off); a routed track with its bus off stays in the main mix (peak 0.543); with the bus on, the main output is silent (5e-12) and the bus equals the engine's routed output (2.4e-7); all earlier sections pass |
| pluginval strictness 10 | Linux (with editor) and Windows under Wine (editor skipped as before): SUCCESS |
| `tools/fuzz_parsers.cpp` (ASan/UBSan, float-cast-overflow) | 36 000 mutated inputs including kits, song / chain / outputs keys, stereo sliced samples, recording and track outputs while playing, host relocation, detection on mutated WAVs: no crash, 0 runtime errors |
| CPU | stress test A/B with 1.3.1 on the same VM: averages within the run-to-run spread (worst case 9.3-10.3 % vs 9.6 %) |
| MIDI clock end to end: Linux standalone under JACK (dummy driver, 1024-sample period), clock from a JACK client (`tools/jack_clock_test.c`), output recorded and compared with the engine driven by the ideal clock position (`tools/clock_ref.cpp`) | Start: the 14 s recording is bit-identical (0 samples differ). Stop at 4 s, Continue with song position 33 at 6 s: bit-identical. The comparison needs the standalone's own engine time: the engine's timing of events that fall exactly on an integer sample depends on its absolute time (documented since 1.2.0; the 1.3.1 engine behaves the same), so the reference is searched over engine time offsets (found: 3.75 s). With a 256-sample period the VM produced xruns (UI rendered in software on 2 cores) and the recording then differed, as it must |
| Windows exe under Wine, real mouse and keyboard | a stereo WAV with 4 cue points written into the library auto-loads as stereo with 4 slice points; AUTO; DETECT finds 8 points 1 ms before the 8 hits; RESAMPLE MAIN, 1 BAR, NOW while playing: 87 273 samples (1 bar at 132 BPM), stereo, written to Library/Sounds/Resampled and loaded onto track 1; + NEW category, SAVE KIT (3 samples written into "Break Kit.samples"), factory METAL loaded, the user kit loaded back: all 8 tracks and their samples restored; CHN, P2 P3 P2 P4: played in that order, looping, at each pattern's tempo; SONG: two rows (P2 x2, P4 with track 1 muted), SNG, PLAY: rows in order with the repeat count shown, looping |

Not verified here: clock out to a real MIDI port (no MIDI device in this VM or in Wine; the tick generator is tested,
the sending thread and RtMidi output are not); multi-out and the song in Ableton Live itself (minimal VST3 host and
pluginval only); the plugin's LAB > PROJECT > OUTPUTS view on screen (the standalone, where it is hidden, was checked).

## E. Fixed in 1.3.1

Found in an audit of 1.3.0 (each one reproduced before it was fixed) and checked again after the fix.

| bug | before (1.3.0) | check after the fix (1.3.1) |
|---|---|---|
| B1: renaming or moving a category folder auto-loaded one of its sounds onto the selected track (the watcher knew files by path) | Windows exe under Wine, Auto-load on: `Kicks` and `打击乐 Perc` renamed from outside -> track 1 replaced by "镲片 Hat 01" | same steps: tracks unchanged (autosave compared before / after / renamed back). Still loads: a new WAV in INBOX, a WAV re-rendered in place with another length, the sound's own WAV re-rendered (a sound file is now known by its name, size and time, its WAV included) |
| B2: the session sample cache only grew (every sample ever loaded stayed in memory) | VST3 host test section 7 (5 host states, each with 4 new 60 s float samples = 46 MB, then a state without samples): +220 MB kept, Linux and Windows DLL | +35 MB kept on both (the last state's samples until the audio thread has let go of them; freed at the next change). In the standalone the samples referred to by the 60 undo steps stay, the rest is released every 5 s. `sample_test` K: pinned / in-use / unused entries |
| B3: undefined behaviour on out-of-range numbers in files (float -> int casts of e.g. `1e308` in kit, lengths, locks, master) | `fuzz_parsers` built with `-fsanitize=float-cast-overflow`: runtime errors at 4 sites | 30 000 mutated inputs (projects with samples, sounds, grooves, control.json, WAV): 0 runtime errors, no crash; `control_test` 61 809 checks under ASan/UBSan |
| B4: export turned non-ASCII track / sound names into `_` | `底鼓` -> `__` in the zip | names keep their UTF-8 letters, zip entries with such names carry the UTF-8 flag (bit 11); ASCII names unchanged: the 20 export zips of the 10 test projects are byte-identical to 1.3.0, and the comparison with the web app is unchanged (0 failures, 201 of 39.9 M samples 1 LSB) |
| B5: a WAV was read into memory whole before the 60 s cut (a 2 GB file = 2 GB allocated) | — | only the header and at most 60 s of audio are read; "cut" is reported only when the audio really is longer (a 50 s file followed by a 1 MB chunk is not "cut"; audio after 5 MB of other chunks is found); out of memory is reported instead of ending the program. `sample_test` K, native (ASan/UBSan) and Windows build under Wine: 149 checks pass |
| P1: autosave built and wrote the whole project (with base64 samples) on the UI thread every 2 s after a change | 16 x 20 s float samples: 238 ms of UI time per save (compare, build and write 78 MB) | the UI thread compares without sample data and copies the project (1.3 ms for the same project, mean of 5; samples are shared, not copied); a worker thread builds and writes it (237 ms). On exit the program waits for a running write and saves the last state itself: a sound auto-loaded 1.6 s before closing (`taskkill` = WM_CLOSE) was in the autosave after the exit |

Unchanged by these fixes (rerun on the 1.3.1 binaries): engine equivalence with the web app, 36 scenarios at 44.1 / 48 / 96 kHz
(max 5.96e-8); generator 1001 objects / 694 715 values, 0 mismatches; presets through the UI path 0/111; host echo 0/111;
preview 0/222; sync drift <= 1 sample; `core_test` (web round trip, 0 heap allocations on the audio thread); VST3 host
test on Linux and on the Windows DLL under Wine: PASSED; pluginval strictness 10 on both: SUCCESS.

Seen while testing, not changed: two WAV files with exactly the same audio are one sample (samples are known by their
content), so the second one shows the first one's sample name on the SAMPLE page; the track shows the right sound name.

## D. New in 1.3.0: LAB > CONTROL (standalone keyboard and MIDI-controller mapping)

| check | result |
|---|---|
| `tools/control_test.cpp` (ASan/UBSan): every key 1..0xE100 of the default map against the 1.2 keyboard handler; default notes for 16 channels; all 85 action ids parse back; bad ids refused; relative decoding (2's complement, sign bit, offset 64); rebinding a source; key stealing; JSON round trip; junk in control.json skipped | 61809 checks pass; the 33 default keys and the C1-G1 / C0-G0 notes are exactly the 1.2 behaviour |
| Windows exe under Wine, real keys: KEY of PLAY clicked, P pressed, P pressed again; KEY of UNDO clicked, Space pressed | P plays; Space moves from PLAY / STOP to UNDO ("WAS PLAY / STOP"); control.json updated; default keys (2 = step toggle, Shift+3 = trigless) behave as before |
| MIDI end to end in a test build of the exe (`-DHARDER_TEST_MIDI_INJECT`: messages read from a file enter the audio thread exactly where RtMidi's do): MIDI LEARN + click on the TUNE encoder + CC 21 | CC 21 = ENCODER 1; CC 21 = 0 / 127 set TUNE to -24 / +24 st; after switching the mapping to REL 2C, five 127s = -5 |
| FIXED PARAM learned on STRT of track 1, then track 2 selected, CC 22 = 127 | track 1 STRT = 127 |
| note C3 learned on PLAY, CC 64 (button) on STOP | the note starts, CC 64 >= 64 stops |
| note C2 learned on step 1; note on, three relative +1 on ENCODER 1, note off | step 1 gets a TUNE lock of base + 3 (an Elektron-style lock from the controller), the step is not toggled |
| default pad notes (audio thread): C#1 velocity 127 and 40 | sound recorded from the device, peak 0.554 vs 0.155; with REC on and playing, a D1 at velocity 80 is recorded as a trig with a velocity lock of 80 |
| restart | mappings and modes restored from control.json |
| plugin builds | MIDI path unchanged (fixed notes); VST3 host test and pluginval as below |

Not verified here: a physical MIDI controller (no MIDI device in this VM; the RtMidi input itself is the 1.1 code);
key names on a real French layout (Windows asks the active layout through GetKeyNameText; Wine reports US names).

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

## 3. Plugin validation (2.0.0 binaries)

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
