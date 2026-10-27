// End-to-end MIDI clock test of the Linux standalone (JACK): this client sends MIDI clock (two beats of ticks while
// stopped, Start, ticks; optionally Stop and Continue with a song position) and records the standalone's main output.
// tools/clock_ref.cpp then renders the same project as a host would drive it and compares sample by sample.
//   gcc -O2 tools/jack_clock_test.c /usr/lib/x86_64-linux-gnu/libjack.so.0 -lm -o OUT/jack_clock_test
//   jackd -n t -d dummy -r 48000 -p 1024 &   JACK_DEFAULT_SERVER=t bin/HARDER &   (settings.json: "clockIn": true)
//   OUT/jack_clock_test 125 14 rec.f32 [jitter_samples] [stop_after_s continue_after_s]
//   jack_connect jack_clock_test... : clk:midi_out -> HARDER:events-in, HARDER:main_l/r -> clk:in_1/2
//   OUT/clock_ref autosave.json 125 70080 rec.f32 1024 1024 [stopT contT spp16]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
typedef struct _jack_client jack_client_t; typedef struct _jack_port jack_port_t; typedef uint32_t jack_nframes_t;
typedef int (*JackProcessCallback)(jack_nframes_t, void*);
extern jack_client_t* jack_client_open(const char*, int, int*, ...);
extern int jack_set_process_callback(jack_client_t*, JackProcessCallback, void*);
extern jack_port_t* jack_port_register(jack_client_t*, const char*, const char*, unsigned long, unsigned long);
extern void* jack_port_get_buffer(jack_port_t*, jack_nframes_t);
extern int jack_activate(jack_client_t*); extern int jack_deactivate(jack_client_t*); extern int jack_client_close(jack_client_t*);
extern void jack_midi_clear_buffer(void*); extern int jack_midi_event_write(void*, jack_nframes_t, const unsigned char*, size_t);
extern jack_nframes_t jack_get_sample_rate(jack_client_t*);
static jack_port_t *mo, *ai[2]; static double tick, nextT = 0, startAt; static long frame = 0, total; static float* rec; static int started = 0, jit = 0;
static double stopAt = -1, contAt = -1; static int stopped = 0, conted = 0; static unsigned rs = 1;
static int proc(jack_nframes_t n, void* a) {
  void* mb = jack_port_get_buffer(mo, n); jack_midi_clear_buffer(mb);
  float* in0 = jack_port_get_buffer(ai[0], n); float* in1 = jack_port_get_buffer(ai[1], n);
  for (jack_nframes_t i = 0; i < n; i++) if (frame + i < total) { rec[(frame + i) * 2] = in0[i]; rec[(frame + i) * 2 + 1] = in1[i]; }
  const double end = frame + n;
  if (!started && startAt < end) { unsigned char b = 0xFA; jack_midi_event_write(mb, (jack_nframes_t)(startAt > frame ? startAt - frame : 0), &b, 1); started = 1; }
  if (stopAt > 0 && !stopped && stopAt < end) { unsigned char b = 0xFC; jack_midi_event_write(mb, (jack_nframes_t)(stopAt - frame), &b, 1); stopped = 1; }
  if (contAt > 0 && !conted && contAt < end) {   // song position = the 16th where the stop happened (rounded down), then Continue
    long spp = (long)((stopAt - startAt) / (tick * 6)); unsigned char p[3] = { 0xF2, (unsigned char)(spp & 127), (unsigned char)(spp >> 7) };
    jack_midi_event_write(mb, (jack_nframes_t)(contAt - frame), p, 3); unsigned char b = 0xFB; jack_midi_event_write(mb, (jack_nframes_t)(contAt - frame), &b, 1); conted = 1;
    fprintf(stderr, "continue from 16th %ld\n", spp); }
  while (nextT < end) {
    double t = nextT; if (jit) { rs = rs * 1664525u + 1013904223u; t += ((rs >> 8) / 16777216.0 * 2 - 1) * jit; }
    long f = (long)floor(t) - frame; if (f < 0) f = 0; if (f >= (long)n) f = n - 1;
    unsigned char b = 0xF8; jack_midi_event_write(mb, (jack_nframes_t)f, &b, 1); nextT += tick;
  }
  frame += n; return 0;
}
int main(int c, char** v) {
  double bpm = atof(v[1]), secs = atof(v[2]); if (c > 4) jit = atoi(v[4]); if (c > 6) { stopAt = atof(v[5]); contAt = atof(v[6]); }
  jack_client_t* cl = jack_client_open("clk", 0, NULL); if (!cl) { fprintf(stderr, "no jack\n"); return 1; }
  const double sr = jack_get_sample_rate(cl); tick = sr * 60 / bpm / 24; total = (long)(secs * sr); rec = calloc(total * 2, sizeof(float));
  nextT = sr * 0.5; startAt = nextT + 48 * tick - 100;   // two beats of ticks while stopped, Start 100 samples before tick 48
  if (stopAt > 0) { stopAt = startAt + 100 + stopAt * sr; contAt = startAt + 100 + contAt * sr; }
  mo = jack_port_register(cl, "midi_out", "8 bit raw midi", 2, 0);
  ai[0] = jack_port_register(cl, "in_1", "32 bit float mono audio", 1, 0); ai[1] = jack_port_register(cl, "in_2", "32 bit float mono audio", 1, 0);
  jack_set_process_callback(cl, proc, NULL); jack_activate(cl);
  printf("start_tick_frame %.3f tick %.6f\n", startAt + 100, tick); fflush(stdout);
  while (frame < total) usleep(20000);
  jack_deactivate(cl); FILE* f = fopen(v[3], "wb"); fwrite(rec, sizeof(float), total * 2, f); fclose(f); jack_client_close(cl); return 0;
}
