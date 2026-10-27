# HARDER — standalone program and VST3 plugin (source)

C++ port of the HARDER web groovebox (synth percussion & groove box), built with DPF: `HARDER.exe` (standalone) and
`HARDER.vst3` (plugin) from the same code.

```
plugins/Harder/   Engine.hpp      DSP engine, line-by-line port of the web engine.js
                  Params.hpp      parameter tables and value formatting
                  Model.hpp       project model, groove generator, presets/kits, JSON (web-compatible)
                  Data.hpp        GENERATED from the web sound + groove library (tools/gen_data.js)
                  Export.hpp      loop / kit export (WAV, MIDI, zip), port of the web export
                  Sample.hpp      SAMPLE machine data: WAV decoding, stored formats, ids, base64 (not in the web app)
                  Library.hpp     the shared user library (folders = categories, sound / groove / kit files), docs/LIBRARY.md
                  Clock.hpp       MIDI clock follower (alpha-beta tracker) and clock generator (standalone)
                  FileSys.cpp/hpp UTF-8 file helpers (wide-character API on Windows)
                  Control.hpp     standalone keyboard / MIDI-controller mappings (LAB > CONTROL, control.json)
                  HostParams.hpp  parameters exposed to the host
                  HarderShared.hpp lock-free command link UI -> audio, host parameter mirror and ownership
                  HarderPlugin.cpp DPF plugin (audio thread, Live transport / MIDI clock sync, MIDI, state, track outputs)
                  HarderUI.cpp    NanoVG UI (main face + LAB drawer: SAMPLE, KITS, SONG...; AUDIO, CONTROL in the standalone)
                  res/            app icon (tools/make_icon.py)
tools/            equivalence, generator, export, host-parameter, VST3-host, sample/library and CPU tests
docs/LIBRARY.md   the library format for other programs (sounds, grooves, WAV, parameter tables)
dpf/              DPF with small patches, each behind a define set in plugins/Harder/Makefile:
                  DPF_PARAM_TEXT_HOOK       parameter value text from the plugin
                  DPF_VST3_NO_MIDI_CC_PARAMS no 2080 hidden MIDI-CC parameters
                  DPF_PARAM_REFUSAL         last queued value wins; refused host values reported back
                  DPF_NATIVE_AUDIO_SETTINGS standalone: device/buffer/MIDI selection, sample-rate callback,
                                            start without a stream when no device opens, driver errors
                                            reported instead of thrown from the driver thread
                  DPF_RTAUDIO_WASAPI_NO_MF  no Media Foundation: WASAPI at the device mix rate
                  DPF_RTAUDIO_WASAPI_JOIN_THREAD stop/abort wait for the stream thread (reopen race)
                  DPF_WIN_SAVE_DEFAULT_NAME Windows save dialog: suggested name, overwrite prompt
                  DPF_VST3_AUX_OUTPUT_GROUPS VST3: output port groups after the first are aux buses (off by
                                            default); an output bus the host does not use reaches the plugin as nullptr
                  (DPF_NATIVE_AUDIO_SETTINGS also: MIDI output ports for messages sent at once, MIDI clock let through
                   RtMidi and handed to the plugin on arrival via dpf_midi_realtime_hook)
```

Download a ready-made build from the Releases page (`HARDER-<version>-win64.zip`: HARDER.exe + HARDER.vst3, see
INSTALL.txt inside).

Getting the source: `git clone --recurse-submodules <repo>` and then `scripts/setup-dpf.sh` (or just run the script after
a plain clone). `dpf/` is the upstream DPF repository pinned as a submodule; the script applies
`patches/dpf-harder.patch` to it.

Build (Linux host): `scripts/build.sh windows` (MinGW-w64, static: bin-windows/HARDER.vst3 and HARDER.exe) or
`scripts/build.sh linux` (bin/HARDER.vst3 and the standalone bin/HARDER). `scripts/package.sh` writes dist/.

Tests (need the web build in `$HARDER_WEB`, default `/home/claude/strobe`):

```
tools/build_tools.sh OUT              # builds render, stress, core_test, gen_test against libhmath
node tools/scen.js OUT 48000          # scenarios + JS reference renders
OUT/render OUT/x.json x.cpp.f32
node tools/compare.js OUT/x.js.f32 x.cpp.f32 48000
OUT/gen_test | node tools/gen_test.js
OUT/stress; OUT/core_test web_default.json
OUT/sync_test
OUT/preset_path_test 0 1                # all 111 sounds through the UI path + command queue
OUT/host_echo_test 1                    # same under a model of DPF + Ableton Live's stale parameter values
node tools/preview_test.js prev.f32 && OUT/preview_test prev.f32   # OLED preview vs web renderOneShot
OUT/vst3_host_test bin/HARDER.vst3/Contents/x86_64-linux/HARDER.so  # real binary in a minimal VST3 host
node tools/export_ref.js EXP && OUT/export_test EXP default kit0 ... poly && node tools/export_compare.js EXP
OUT/live_expect project.json pre n out.f32  # expected standalone output after PLAY (live recording test)
g++ -std=gnu++17 -O1 -fsanitize=address,undefined -pthread tools/sample_test.cpp plugins/Harder/FileSys.cpp OUT/libhmath.a -o OUT/sample_test
OUT/sample_test <empty dir>              # WAV formats, SAMPLE playback, projects with samples, library, sample lifetime
g++ -std=gnu++17 -O1 -fsanitize=address,undefined -pthread tools/v2_test.cpp plugins/Harder/FileSys.cpp OUT/libhmath.a -o OUT/v2_test
OUT/v2_test <empty dir>                  # 2.0: slices, stereo, chain / song (+ host positions), resampling, track outputs,
                                         # MIDI clock in / out, kits, WAV cue points, transient detection, project keys
OUT/fuzz_parsers 36000 <empty dir>       # mutated projects / sounds / grooves / kits / control.json / WAV (build with sanitizers)
g++ -std=gnu++17 tools/control_test.cpp -o OUT/control_test && OUT/control_test   # default keys = the old fixed keys, ids, JSON, relative CC
HARDER_CXXFLAGS=-DHARDER_TEST_MIDI_INJECT scripts/build.sh windows   # TEST build only: Documents/HARDER/midi-inject.txt (hex lines) is fed in as MIDI input
```

`vst3_host_test.cpp` also builds with MinGW (`-static`, against a MinGW libhmath) and runs under Wine against the
Windows DLL.

Host parameters: values the plugin sets itself (sound/kit/project loads, knobs) are owned by the plugin for 1 s
(`Core::own`); a disagreeing host value in that window is refused and the plugin's value is reported back, so
stale values a host sends while an edit is in flight cannot overwrite a freshly loaded sound.

The DSP must be compiled without `-ffast-math` (the Makefile overrides DPF's default): the engine relies on
IEEE semantics for its NaN guards and for bit-level agreement with the web engine.

## License

HARDER is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, version 3 or (at your option) any later version. See `LICENSE`.
Copyright (C) 2026 pislyc.

Third-party parts keep their own licences: DPF (ISC, `dpf/LICENSE`), RtAudio / RtMidi (MIT-style, in `dpf/`),
musl math routines (MIT, `plugins/Harder/hmath/COPYRIGHT.musl`), fonts (SIL Open Font License, `fonts/OFL-*.txt`).

Samples: the audio thread reads sample data through raw pointers in the engine's slot table (`Engine::smp`). The main
thread keeps every sample the engine may still read alive (`Core::engSmp`, `Core::retired`) until the command that
replaced it has been consumed by the audio thread (`Link::consumed`); after a queue overflow, everything is kept until
the full resend has been applied. `tools/sample_test` checks this under ASan with an audio thread swapping samples.

Standalone MIDI: the audio thread plays the notes mapped to pads itself (`Core::padMap`, no UI latency) and passes every
note and CC to the UI through a lock-free ring (`Core::midiPush` / `midiPop`); the UI runs the mapped actions (the same
functions the mouse and the keyboard use) and handles MIDI learn. In the plugin the fixed notes (C1-G1 pads, C0-G0
patterns) apply as before.
