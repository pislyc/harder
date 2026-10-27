# HARDER library format (version 1, HARDER 2.0)

The HARDER standalone program and the HARDER plugin share one library folder. Any other program — a synth, a
sound-design tool, a script — can put sounds and grooves there, and HARDER lists them within about two seconds.
This file is the contract for such programs.

**中文概要：** 库在 `文档\HARDER\Library\`（Linux：`~/.config/HARDER/Library/`）。`Sounds/`、`Grooves/` 和 `Kits/` 下的每个子文件夹就是一个分类，直接放在 `Sounds/` 下的文件归到 INBOX。外部软件写一个 WAV 就能被 HARDER 当作 SAMPLE 音色使用；要连同包络、滤波等设置一起交给 HARDER，就再写一个同名的 `.json` 音色文件（格式见下）。请先写成临时文件名，写完再改名。在 HARDER 里勾选 "Auto-load new" 后，新文件或被覆盖的文件会自动载入当前选中的轨道。

## Where

| OS | folder |
|---|---|
| Windows | `Documents\HARDER\Library\` (the user's Documents folder, `%USERPROFILE%\Documents` unless moved) |
| Linux | `~/.config/HARDER/Library/` (`$XDG_CONFIG_HOME/HARDER/Library/` if set) |

HARDER creates `Library/`, `Library/Sounds/`, `Library/Grooves/`, `Library/Kits/` and a short `README.txt` the first
time the user library is opened.

## Layout

```
Library/
  Sounds/
    <name>.wav | <name>.json          INBOX (files directly in Sounds/)
    <category>/<name>.wav             plain WAV: a SAMPLE sound with default settings
    <category>/<name>.json            HARDER sound, any machine
    <category>/<name>.json + <name>.wav   a SAMPLE sound and its sample (the WAV is not listed on its own)
    Resampled/<name>.wav              RESAMPLE (LAB > SAMPLE) writes its recordings here
  Grooves/
    <name>.json                       UNSORTED
    <category>/<name>.json            HARDER groove (one pattern)
  Kits/
    <category>/<name>.json            HARDER kit (eight sounds, optionally the master FX)
    <category>/<name>.samples/*.wav   the kit's samples (not a category, not listed)
  Trash/                              what HARDER deletes is moved here
```

- A category is a folder directly inside `Sounds/`, `Grooves/` or `Kits/` (one level; deeper folders are ignored).
- The name shown is the `"name"` field of a `.json`, else the file name without its extension.
- File and folder names are UTF-8 on every system. Characters `\ / : * ? " < > |` are not used by HARDER.
- Hidden files and names starting with `.` are ignored, so are other extensions (`.tmp`, `.part`, ...).

## Writing files safely

HARDER may look at the folder while a file is being written. Write to a temporary name that HARDER ignores
(for example `kick.wav.tmp`) and rename it to `kick.wav` when it is complete. For a sound with its own sample, write
the `.wav` first, then the `.json`. With auto-load on, HARDER loads a file once its size and modification time have
stayed the same over two scans 0.7 s apart, so a file written in place usually works too, but the rename is the
reliable way. Overwriting an existing file with new content (same name) counts as a new version: with auto-load on,
HARDER loads it again — handy for "render, listen, tweak, render again".

## WAV files

RIFF/WAVE, PCM 8/16/24/32-bit or IEEE float 32/64-bit, `WAVE_FORMAT_EXTENSIBLE` with a PCM or float sub-format,
any sample rate from 1 kHz to 768 kHz. Mono and stereo files keep their channels (since 2.0; a stereo sample runs
through the voice's filter, drive and envelope on both sides, and PAN works as a balance); other channel counts are
mixed to mono. HARDER keeps the file's rate (the voice resamples with 4-point Hermite interpolation). Up to 60 s;
longer files are cut at 60 s. Compressed formats (ADPCM, MP3 in WAV, ...) and AIFF/FLAC are not read.

Cue points (`cue ` chunk, `dwSampleOffset`) are slice points: SLCS = AUTO plays the slices between them. HARDER
writes a sample's slice points back as cue points when it saves a WAV (SAVE SOUND, kits, resampling), so a program
that marks the hits of a break can hand them over in the file itself.

Inside a project, a sample is stored as 16-bit if the file was 8/16-bit, 24-bit if it was 24/32-bit integer, and
32-bit float if it was float, so a project renders the same before and after it is saved. A resampled recording is
stored as 32-bit float.

## Sound file (`.json` in `Sounds/`)

```json
{
  "format": "harder-sound",
  "version": 1,
  "name": "Deep Kick",
  "machine": "SMP",
  "p": [0, 0, 0, 0, 127, 0, 0, 100,   127, 0, 0, 0, 0, 40, 0, 0,   0, 0, 60, 0, 0, 100, 0, 0],
  "sample": { "file": "Deep Kick.wav" }
}
```

| key | | |
|---|---|---|
| `format` | required | `"harder-sound"` |
| `version` | optional | `1`. Files with a higher version are refused (made by a newer HARDER). |
| `name` | optional | display name |
| `machine` | required | one of `SUB FMK SNR CLP MTL MDL ZAP GLT DUO SMP` |
| `p` | optional | up to 56 parameter values by index (tables below). Missing entries, `null` and non-numbers take the default; values are clamped to their range. Fractions are allowed where the range is wide (e.g. `63.5`). |
| `sample` | `SMP` only | `{"file": "<name>.wav"}` — a WAV next to the `.json` (no folders, no `..`), or the embedded form `{"name","rate","len","fmt","data"}` used in project files (see below) |

When a sound is loaded onto a track, HARDER keeps that track's choke group (`p[22]`) and duck amount (`p[23]`), like
it does for its factory presets; the SAMPLE slot (`p[2]`) is assigned by HARDER.

### Parameters 8-50 (every machine)

The UI shows these values formatted (Hz, ms, %); the file holds the raw values below.

| index | page | name | range | default |
|---|---|---|---|---|
| 8 | FLTR | FREQ | 0 … 127 | 127 (20.0k) |
| 9 | FLTR | RESO | 0 … 127 | 0 (0%) |
| 10 | FLTR | TYPE | 0 … 127 | 0 (LP) |
| 11 | FLTR | ENV | -64 … 63 | 0 (0) |
| 12 | FLTR | FATK | 0 … 127 | 0 (0) |
| 13 | FLTR | FDEC | 0 … 127 | 40 (41.1ms) |
| 14 | FLTR | KTRK | 0 … 127 | 0 (0%) |
| 16 | AMP | ATK | 0 … 127 | 0 (0) |
| 17 | AMP | HOLD | 0 … 127 | 0 (0) |
| 18 | AMP | DEC | 0 … 127 | 127 (INF) |
| 19 | AMP | DRV | 0 … 127 | 0 (0%) |
| 20 | AMP | PAN | -64 … 63 | 0 (C) |
| 21 | AMP | VOL | 0 … 127 | 100 (100) |
| 22 | AMP | CHOK | 0 … 4 | 0 (OFF) |
| 23 | AMP | DUCK | 0 … 127 | 0 (0%) |
| 24 | FX | DLY | 0 … 127 | 0 (0) |
| 25 | FX | REV | 0 … 127 | 0 (0) |
| 26 | FX | BITS | 1 … 16 | 16 (OFF) |
| 27 | FX | SRR | 0 … 127 | 0 (OFF) |
| 28 | FX | COMB | 0 … 127 | 0 (0%) |
| 29 | FX | CTUN | 24 … 108 | 60 (C4) |
| 30 | FX | CFB | 0 … 127 | 96 (76%) |
| 32 | LFO | SPD | 0 … 13 | 4 (8st) |
| 33 | LFO | DEP | -64 … 63 | 0 (0) |
| 34 | LFO | DEST | 0 … 15 | 8 (FREQ) |
| 35 | LFO | WAVE | 0 … 6 | 0 (SIN) |
| 36 | LFO | MODE | 0 … 3 | 1 (TRIG) |
| 37 | LFO | PHAS | 0 … 127 | 0 (0°) |
| 40 | TRIG | NOTE | -24 … 24 | 0 (0) |
| 41 | TRIG | VEL | 1 … 127 | 100 (100) |
| 42 | TRIG | PROB | 0 … 100 | 100 (100%) |
| 43 | TRIG | COND | 0 … 17 | 0 (—) |
| 44 | TRIG | MICR | -23 … 23 | 0 (0) |
| 45 | TRIG | RTRG | 0 … 7 | 0 (OFF) |
| 46 | TRIG | RFAD | -64 … 63 | 0 (0) |
| 47 | TRIG | RLEN | 0 … 5 | 0 (1st) |
| 48 | STEP | SLID | 0 … 1 | 0 (OFF) |
| 49 | STEP | LCND | 0 … 17 | 0 (—) |
| 50 | STEP | LPRB | 0 … 100 | 100 (100%) |

Indices 15, 31, 38, 39, 51-55 are unused (0). 43/49 are condition indices (0 = none, 1 FILL, 2 !FIL, 3 PRE, 4 !PRE,
5 NEI, 6 !NEI, 7 1ST, 8 !1ST, 9 1:2, 10 2:2, 11 1:3, 12 2:3, 13 3:3, 14 1:4, 15 2:4, 16 3:4, 17 4:4); 40-50 are the
defaults for new trigs and are usually left at their defaults in a sound.

### Parameters 0-7 (per machine)

| machine id | name | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|---|
| SUB | BD SUB | TUNE 0…127 (40) | SWEP 0…127 (60) | BEND 0…127 (44) | DEC 0…127 (62) | CLIK 0…127 (50) | CTON 0…127 (64) | FOLD 0…127 (0) | HARM 0…127 (0) |
| FMK | BD FM | TUNE 0…127 (38) | SWEP 0…127 (52) | BEND 0…127 (36) | DEC 0…127 (58) | RATI 0…127 (17) | INDX 0…127 (44) | IDEC 0…127 (34) | FDBK 0…127 (0) |
| SNR | SD TONE | TUNE 0…127 (50) | SPRD 0…127 (50) | TDEC 0…127 (44) | NDEC 0…127 (56) | NOIS 0…127 (76) | NTON 0…127 (78) | SNAP 0…127 (60) | SWEP 0…127 (30) |
| CLP | CP GRID | TONE 0…127 (70) | RESO 0…127 (36) | CNT 0…127 (50) | SPAC 0…127 (60) | DEC 0…127 (52) | SCTR 0…127 (12) | RING 0…127 (0) | RFRQ 0…127 (64) |
| MTL | HH METAL | TUNE 0…127 (64) | DEC 0…127 (30) | COLR 0…127 (88) | RESO 0…127 (30) | SPRD 0…127 (64) | NOIS 0…127 (20) | RING 0…127 (0) | BITE 0…127 (40) |
| MDL | PT MODAL | TUNE 0…127 (50) | DEC 0…127 (50) | MATR 0…127 (30) | DAMP 0…127 (50) | STRK 0…127 (64) | POS 0…127 (30) | SWEP 0…127 (8) | NOIS 0…127 (20) |
| ZAP | FX ZAP | TUNE 0…127 (70) | SWEP -64…63 (40) | STIM 0…127 (40) | DEC 0…127 (40) | RATI 0…127 (64) | INDX 0…127 (50) | IDEC 0…127 (50) | FDBK 0…127 (20) |
| GLT | NZ GLITCH | SRC 0…3 (3) | RATE 0…127 (96) | ALGO 0…127 (40) | TONE 0…127 (80) | RESO 0…127 (60) | SWEP -64…63 (30) | STIM 0…127 (40) | DEC 0…127 (40) |
| DUO | PT DUO | TUNE 0…127 (76) | RATI 0…127 (21) | WAVE 0…127 (127) | DEC 0…127 (50) | BPF 0…127 (88) | RESO 0…127 (34) | SWEP 0…127 (0) | CLIK 0…127 (20) |
| SMP | SAMPLE | TUNE -24…24 (0) | FINE -64…63 (0) | SMP 0…63 (0) | STRT 0…127 (0) | END 0…127 (127) | MODE 0…3 (0) | LOOP 0…127 (0) | LEV 0…127 (100) |

SAMPLE (`SMP`) in detail:

| index | name | meaning |
|---|---|---|
| 0 | TUNE | -24 … +24 semitones |
| 1 | FINE | -64 … +63 = -1 … +1 semitone |
| 2 | SMP | sample slot 1-64 (0 … 63); set by HARDER when a sound is loaded |
| 3 | STRT | start, 0 … 127 = 0 … 100 % of the sample |
| 4 | END | end, 0 … 127 = 0 … 100 % (always after STRT) |
| 5 | MODE | 0 one-shot, 1 reverse, 2 loop, 3 reverse loop |
| 6 | LOOP | loop point inside STRT…END, 0 … 127 = 0 … 100 % (from the end in reverse loop) |
| 7 | LEV | level, gain (LEV/100)², 100 = unity |

The trig NOTE (40) and an LFO on PTCH change the pitch as for every machine. AMP DEC (18) defaults to INF, so the
sample plays to its END unless the amp envelope is shortened.

Slices (2.0), on the STEP page of the SAMPLE machine only (other machines do not use these indices):

| index | name | meaning |
|---|---|---|
| 51 | SLCS | 0 off, 1-4 = 8 / 16 / 32 / 64 equal slices, 5 AUTO = the sample's slice points |
| 52 | SLCE | 0 = the slice of the trig's step (step n plays slice n, wrapping), 1-64 = that slice |

With slices on, STRT / END / LOOP are positions inside the slice. Lock SLCE on a step to play another slice there.

## Groove file (`.json` in `Grooves/`)

```json
{ "format": "harder-groove", "version": 1, "name": "Broken 02", "pattern": { ... } }
```

`pattern` is one entry of the `patterns` array of a HARDER / web-groovebox project file (`harder-project.json`):

| key | |
|---|---|
| `mlen` | master length in steps, 1-64 |
| `len` | 9 numbers: track length per track (8 tracks + FX track), 1-64 |
| `scl` | 9 numbers: speed index per track, 0-6 = 1/8 1/4 1/2 3/4 1 3/2 2 (4 = normal) |
| `steps` | 9 arrays of 64: `null` (no trig) or `{ "tl": 1 (trigless lock, optional), "l": { "<param index>": value, ... } }` |
| `bpm`, `swing` | tempo (40-300) and swing (50-80) |

Loading a groove replaces the current pattern (undo brings it back). Sound locks (`l` with indices 0-7) are
machine-specific values and apply to whatever machine the track has.

## Project files

A project (`harder-project.json`, the standalone's `autosave.json`, the state inside a Live set) that uses SAMPLE
tracks carries its samples, so it can be moved to another computer as one file:

```json
"samples": [ { "slot": 0, "id": "715e3411fbbe0b51", "name": "Lower Kick", "rate": 48000, "len": 28800, "fmt": "s24", "data": "<base64>" },
             { "slot": 1, "id": "…", "name": "Break", "rate": 44100, "len": 88200, "fmt": "s16", "ch": 2, "slices": [0, 11025, 22050], "data": "<base64>" } ]
```

`data` is the sample in little-endian `s16`, `s24` (3 bytes) or `f32`, base64-encoded, interleaved L R when `ch` is 2
(`ch` is left out for mono); `slices` are slice start frames, ascending. `id` is a 64-bit FNV-1a hash of the rate, the
format, the data and (2.0, stereo or sliced samples only) the channel count and slice points: mono samples without
slices keep the ids they had in 1.x. The key is only written when a slot is used, so projects without samples are
unchanged and still open in the web groovebox; a project with SAMPLE tracks does not open there (the web version has
no SAMPLE machine).

Other keys of 2.0, also written only when used (a project that has none of them is byte-for-byte a 1.x / web project;
the web groovebox ignores them, so such a project still opens there and plays its current pattern):

| key | |
|---|---|
| `seq` | `"chain"` or `"song"`: what PLAY plays (absent = the current pattern) |
| `chain` | pattern indices 0-7, played in order and looped |
| `song` | `{ "rows": [ { "p": 0-7, "n": repeats 1-99, "mute": [track numbers 1-8], "bpm": 40-300 } ], "loop": true/false, "start": row }`; `mute` and `bpm` are optional (no `bpm` = the pattern's tempo) |
| `outs` | 8 numbers, 1 = the track plays on its own output in the plugin (Live: Track n) instead of the main mix |

## Kit file (`.json` in `Kits/`)

```json
{ "format": "harder-kit", "version": 1, "name": "Break Kit",
  "tracks": [ { "name": "Break", "machine": "SMP", "p": [ ... ], "sample": { "file": "Break Kit.samples/1 Break.wav" } },
              { "name": "FM Snare", "machine": "ZAP", "p": [ ... ] }, ... 8 entries ],
  "roles": [ "kick", "snare", ... ], "master": [ 32 values ] }
```

Each track entry is a sound (same keys as a sound file without `format`); a SAMPLE track's `file` is inside the
kit's own folder `<kit name>.samples/`. `roles` (the groove generator's track roles) and `master` (the master FX)
are optional; loading a kit keeps the master volume. Renaming or moving a kit in HARDER moves its samples folder
and rewrites the references.

## Example: a script that hands HARDER a sound

```python
import json, os, struct, math
lib = os.path.join(os.path.expanduser("~"), "Documents", "HARDER", "Library", "Sounds", "From Lower")
os.makedirs(lib, exist_ok=True)
rate, n = 48000, 24000
pcm = b"".join(struct.pack("<h", int(32767 * 0.8 * math.exp(-i / 6000) * math.sin(2 * math.pi * 55 * i / rate))) for i in range(n))
wav = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(pcm)) + pcm
def put(name, data):                       # write, then rename: HARDER never sees a half file
    tmp = os.path.join(lib, name + ".tmp"); open(tmp, "wb").write(data); os.replace(tmp, os.path.join(lib, name))
put("Sub 55.wav", wav)
sound = {"format": "harder-sound", "version": 1, "name": "Sub 55", "machine": "SMP",
         "p": [0, 0, 0, 0, 127, 0, 0, 100, 90, 20], "sample": {"file": "Sub 55.wav"}}   # FREQ 90, RESO 20
put("Sub 55.json", json.dumps(sound, ensure_ascii=False).encode("utf-8"))
```
