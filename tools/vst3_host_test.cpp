// Minimal VST3 host that loads the built HARDER.vst3 (Linux .so, or the Windows DLL under Wine) and checks the
// host-parameter behaviour that broke the sound library in Live, on the real binary:
//  1. a sound loaded into the project (here through set_state) is not reverted by the old value a host sends
//     back (Live re-sends the last value it knew at the start of a gesture); the plugin reports its own value back
//     (output parameter change), and the controller and the saved state show it;
//  2. several points at offset 0 in one queue: the last one counts;
//  3. once the plugin is not editing a parameter, host automation is applied;
//  4. the sound: after each sound load a MIDI note on C1 plays track 1; the audio must equal an engine that got
//     the web app's messages (same model code, same project), sample for sample.
// usage: vst3_host_test <path to HARDER.vst3 binary>
#include "../plugins/Harder/Model.hpp"
#include "../plugins/Harder/HostParams.hpp"
#include "travesty/base.h"
#include "travesty/bstream.h"
#include "travesty/component.h"
#include "travesty/audio_processor.h"
#include "travesty/edit_controller.h"
#include "travesty/events.h"
#include "travesty/factory.h"
#include "travesty/host.h"
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#ifdef _WIN32
# include <windows.h>
#else
# include <dlfcn.h>
#endif
using namespace hd;

static v3_result V3_API qiNo(void*, const v3_tuid, void** o) { *o = nullptr; return V3_NO_INTERFACE; }
static uint32_t V3_API refOne(void*) { return 1; }

// ---- stream
struct Stream { v3_bstream_cpp* vt; std::string data; size_t pos = 0; };
static v3_result V3_API sRead(void* s, void* buf, int32_t n, int32_t* got) { Stream* S = *(Stream**)&s; S = (Stream*)s; const int32_t k = (int32_t)std::min<size_t>(n, S->data.size() - S->pos); std::memcpy(buf, S->data.data() + S->pos, k); S->pos += k; if (got) *got = k; return V3_OK; }
static v3_result V3_API sWrite(void* s, void* buf, int32_t n, int32_t* put) { Stream* S = (Stream*)s; S->data.append((const char*)buf, n); if (put) *put = n; return V3_OK; }
static v3_result V3_API sSeek(void* s, int64_t p, int32_t mode, int64_t* r) { Stream* S = (Stream*)s; int64_t b = mode == 0 ? 0 : mode == 1 ? (int64_t)S->pos : (int64_t)S->data.size(); S->pos = (size_t)std::max<int64_t>(0, b + p); if (r) *r = (int64_t)S->pos; return V3_OK; }
static v3_result V3_API sTell(void* s, int64_t* p) { *p = (int64_t)((Stream*)s)->pos; return V3_OK; }
static v3_bstream_cpp streamVt = { { qiNo, refOne, refOne }, { sRead, sWrite, sSeek, sTell } };

// ---- parameter queues / changes
struct Queue { void* vt; v3_param_id id; std::vector<std::pair<int32_t, double>> pts; };
static v3_param_id V3_API qId(void* s) { return ((Queue*)s)->id; }
static int32_t V3_API qCount(void* s) { return (int32_t)((Queue*)s)->pts.size(); }
static v3_result V3_API qGet(void* s, int32_t i, int32_t* off, double* v) { Queue* Q = (Queue*)s; if (i < 0 || i >= (int32_t)Q->pts.size()) return V3_INVALID_ARG; *off = Q->pts[i].first; *v = Q->pts[i].second; return V3_OK; }
static v3_result V3_API qAdd(void* s, int32_t off, double v, int32_t* idx) { Queue* Q = (Queue*)s; Q->pts.push_back({ off, v }); if (idx) *idx = (int32_t)Q->pts.size() - 1; return V3_OK; }
struct QueueVt { v3_funknown u; v3_param_value_queue q; } queueVt = { { qiNo, refOne, refOne }, { qId, qCount, qGet, qAdd } };
struct Changes { void* vt; std::vector<Queue*> qs; Queue* find(v3_param_id id) { for (Queue* q : qs) if (q->id == id) return q; return nullptr; } void clear() { for (Queue* q : qs) delete q; qs.clear(); } };
static int32_t V3_API cCount(void* s) { return (int32_t)((Changes*)s)->qs.size(); }
static v3_param_value_queue** V3_API cGet(void* s, int32_t i) { Changes* C = (Changes*)s; return i >= 0 && i < (int32_t)C->qs.size() ? (v3_param_value_queue**)C->qs[i] : nullptr; }
static v3_param_value_queue** V3_API cAdd(void* s, const v3_param_id* id, int32_t* idx) {
  Changes* C = (Changes*)s; Queue* q = C->find(*id); if (!q) { q = new Queue(); q->vt = &queueVt; q->id = *id; C->qs.push_back(q); }
  if (idx) for (size_t k = 0; k < C->qs.size(); k++) if (C->qs[k] == q) *idx = (int32_t)k;
  return (v3_param_value_queue**)q;
}
struct ChangesVt { v3_funknown u; v3_param_changes c; } changesVt = { { qiNo, refOne, refOne }, { cCount, cGet, cAdd } };

// ---- events
struct Events { void* vt; std::vector<v3_event> ev; };
static uint32_t V3_API eCount(void* s) { return (uint32_t)((Events*)s)->ev.size(); }
static v3_result V3_API eGet(void* s, int32_t i, v3_event* e) { Events* E = (Events*)s; if (i < 0 || i >= (int32_t)E->ev.size()) return V3_INVALID_ARG; *e = E->ev[i]; return V3_OK; }
static v3_result V3_API eAdd(void* s, v3_event* e) { ((Events*)s)->ev.push_back(*e); return V3_OK; }
static v3_event_list_cpp eventsVt = { { qiNo, refOne, refOne }, { eCount, eGet, eAdd } };

// ---- component handler + host application
static int restarts = 0;
static v3_result V3_API hBegin(void*, v3_param_id) { return V3_OK; }
static v3_result V3_API hPerform(void*, v3_param_id, double) { return V3_OK; }
static v3_result V3_API hEnd(void*, v3_param_id) { return V3_OK; }
static v3_result V3_API hRestart(void*, int32_t) { restarts++; return V3_OK; }
struct HandlerVt { v3_funknown u; v3_component_handler h; } handlerVt = { { qiNo, refOne, refOne }, { hBegin, hPerform, hEnd, hRestart } };
struct Handler { void* vt = &handlerVt; } handler;
static v3_result V3_API aName(void*, v3_str_128 name) { const char* n = "harder-test"; int i = 0; for (; n[i] && i < 127; i++) name[i] = (int16_t)n[i]; name[i] = 0; return V3_OK; }
static v3_result V3_API aCreate(void*, v3_tuid, v3_tuid, void** o) { *o = nullptr; return V3_NOT_IMPLEMENTED; }
struct AppVt { v3_funknown u; v3_host_application a; } appVt = { { qiNo, refOne, refOne }, { aName, aCreate } };
struct App { void* vt = &appVt; } app;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main(int argc, char** argv) {
  if (argc < 2) { std::puts("usage: vst3_host_test <HARDER.vst3 binary>"); return 2; }
#ifdef _WIN32
  HMODULE lib = LoadLibraryA(argv[1]); if (!lib) { std::printf("cannot load %s\n", argv[1]); return 2; }
  typedef bool (*InitFn)(); InitFn init = (InitFn)GetProcAddress(lib, "InitDll"); if (init) init();
  typedef v3_plugin_factory** (*GetF)(); GetF getF = (GetF)GetProcAddress(lib, "GetPluginFactory");
#else
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); if (!lib) { std::printf("cannot load %s: %s\n", argv[1], dlerror()); return 2; }
  typedef bool (*InitFn)(void*); InitFn init = (InitFn)dlsym(lib, "ModuleEntry"); if (init) init(lib);
  typedef v3_plugin_factory** (*GetF)(); GetF getF = (GetF)dlsym(lib, "GetPluginFactory");
#endif
  v3_plugin_factory** fac = getF(); v3_class_info ci; v3_component** comp = nullptr;
  for (int i = 0; i < v3_cpp_obj(fac)->num_classes(fac); i++) {
    v3_cpp_obj(fac)->get_class_info(fac, i, &ci);
    if (!std::strcmp(ci.category, "Audio Module Class")) { v3_cpp_obj(fac)->create_instance(fac, ci.class_id, v3_component_iid, (void**)&comp); break; }
  }
  if (!comp) { std::puts("no component"); return 2; }
  v3_cpp_obj_initialize(comp, (v3_funknown**)&app);
  v3_edit_controller** ctl = nullptr; v3_cpp_obj_query_interface(comp, v3_edit_controller_iid, &ctl);
  if (!ctl) {   // separate controller class
    v3_tuid cid; v3_cpp_obj(comp)->get_controller_class_id(comp, cid); v3_cpp_obj(fac)->create_instance(fac, cid, v3_edit_controller_iid, (void**)&ctl);
    v3_cpp_obj_initialize(ctl, (v3_funknown**)&app);
  }
  v3_cpp_obj(ctl)->set_component_handler(ctl, (v3_component_handler**)&handler);
  v3_audio_processor** proc = nullptr; v3_cpp_obj_query_interface(comp, v3_audio_processor_iid, &proc);
  const double sr = 48000; const int B = 256;
  v3_process_setup setup = { V3_REALTIME, V3_SAMPLE_32, B, sr }; v3_cpp_obj(proc)->setup_processing(proc, &setup);
  v3_cpp_obj(comp)->activate_bus(comp, V3_AUDIO, V3_OUTPUT, 0, 1);
  v3_cpp_obj(comp)->set_active(comp, 1); v3_cpp_obj(proc)->set_processing(proc, 1);

  // parameter ids by title
  std::vector<v3_param_id> pid(HP_COUNT, 0xFFFFFFFF);
  { const int n = v3_cpp_obj(ctl)->get_parameter_count(ctl); int k = 0;
    for (int i = 0; i < n; i++) { v3_param_info inf; v3_cpp_obj(ctl)->get_parameter_info(ctl, i, &inf); if (inf.flags & V3_PARAM_IS_HIDDEN) continue; if (k < HP_COUNT) pid[k++] = inf.param_id; }
    CHECK(k == HP_COUNT, "%d visible parameters, expected %d", k, HP_COUNT); }
  auto norm = [&](int idx, double v) { double mn, mx, df; hostRange(idx, mn, mx, df); return (v - mn) / (mx - mn); };
  auto plain = [&](int idx, double n) { double mn, mx, df; hostRange(idx, mn, mx, df); return mn + n * (mx - mn); };

  std::vector<float> L(B), R(B); float* outs[2] = { L.data(), R.data() };
  v3_audio_bus_buffers bus; bus.num_channels = 2; bus.channel_silence_bitset = 0; bus.channel_buffers_32 = outs;
  Changes in; in.vt = &changesVt; Changes out; out.vt = &changesVt; Events ev; ev.vt = &eventsVt;
  auto process = [&](std::vector<float>* rec) {
    v3_process_data d; std::memset(&d, 0, sizeof d); d.process_mode = V3_REALTIME; d.symbolic_sample_size = V3_SAMPLE_32; d.nframes = B;
    d.num_output_buses = 1; d.outputs = &bus; d.input_params = (v3_param_changes**)&in; d.output_params = (v3_param_changes**)&out; d.input_events = (v3_event_list**)&ev;
    out.clear(); v3_cpp_obj(proc)->process(proc, &d); in.clear(); ev.ev.clear();
    if (rec) rec->insert(rec->end(), L.begin(), L.end());
  };
  auto setStateJson = [&](const std::string& json) {
    // DPF's chunk: strings separated by NUL, terminated by 0xFE
    Stream s; s.vt = &streamVt; const std::string z(1, '\0');
    s.data = "__dpf_state_begin__" + z + "project" + z + json + z + "__dpf_state_end__" + z + "\xfe";
    return v3_cpp_obj(comp)->set_state(comp, (v3_bstream**)&s);
  };
  auto getProject = [&](Project& P) { Stream s; s.vt = &streamVt; v3_cpp_obj(comp)->get_state(comp, (v3_bstream**)&s);
    const size_t a = s.data.find(std::string("project") + '\0'); if (a == std::string::npos) return false; const size_t b = s.data.find('\0', a + 8);
    return projectFromString(s.data.substr(a + 8, b - a - 8), P); };
  auto ctlPlain = [&](int idx) { return plain(idx, v3_cpp_obj(ctl)->get_parameter_normalised(ctl, pid[idx])); };

  // reference engine fed with the web app's messages
  std::unique_ptr<Project> P(new Project()); defaultProject(*P);
  std::unique_ptr<Engine> W(new Engine()); W->init(sr);
  auto loadW = [&](const Project& Q) { for (int t = 0; t < NT; t++) W->loadTrack(t, Q.tracks[t].m, Q.tracks[t].p); W->loadMaster(Q.master); for (int i = 0; i < NPAT; i++) W->setPattern(i, Q.patterns[i]); W->loadDone(Q.cur, Q.bpm, Q.swing, Q.mutes); };
  CHECK(setStateJson(projectToString(*P)) == V3_OK, "set_state default"); loadW(*P);
  for (int k = 0; k < 8; k++) process(nullptr);
  { std::vector<float> wl(B), wr(B); for (int k = 0; k < 8; k++) W->process(wl.data(), wr.data(), B); }

  int soundBad = 0, revertBad = 0, reportBad = 0; double worst = 0;
  const int tr = 0; const int presets[] = { 5, 17, 33, 48, 60, 77, 91, 104, 110, 2, 40, 70 };
  for (int pi : presets) {
    // load sound pi into track 1: the project changes (here via set_state, as a host preset load or the UI does)
    std::unique_ptr<Project> before(new Project()); *before = *P;
    TrackM& t = P->tracks[tr]; double p[NP]; presetParams(pi, p); p[22] = t.p[22]; p[23] = t.p[23];
    t.m = PRESETS[pi].m; std::memcpy(t.p, p, sizeof p); t.pre = PRESETS[pi].n;
    setStateJson(projectToString(*P));
    // the host sends back the values it knew before (gesture start), every exposed parameter of the track
    for (int k = 0; k < NTP; k++) { const int idx = tr * NTP + k; Queue* q = (Queue*)cAdd(&in, &pid[idx], nullptr);
      const double oldPlain = k < 8 ? srcToHost(before->tracks[tr].m, k, before->tracks[tr].p[k]) : before->tracks[tr].p[TP_IDX[k]];
      q->pts.push_back({ 0, norm(idx, oldPlain) }); }
    // and a note on C1 in the same block
    v3_event e; std::memset(&e, 0, sizeof e); e.type = V3_EVENT_NOTE_ON; e.sample_offset = 0; e.note_on.pitch = 36; e.note_on.velocity = 110 / 127.f; e.note_on.note_id = -1; ev.ev.push_back(e);
    std::vector<float> got; process(&got);
    int reported = 0, differ = 0;
    for (int k = 0; k < NTP; k++) {
      const int idx = tr * NTP + k; const double want = k < 8 ? srcToHost(t.m, k, t.p[k]) : t.p[TP_IDX[k]];
      const double oldPlain = k < 8 ? srcToHost(before->tracks[tr].m, k, before->tracks[tr].p[k]) : before->tracks[tr].p[TP_IDX[k]];
      if (std::fabs(oldPlain - want) < 1e-3) continue; differ++;
      if (Queue* q = out.find(pid[idx])) if (std::fabs(plain(idx, q->pts.back().second) - want) < 1e-3) reported++;
    }
    const bool repBad = reported != differ;
    for (int k = 0; k < 15; k++) process(&got);
    // reference
    loadW(*P); W->audition(tr, 110, nullptr); std::vector<float> ref; { std::vector<float> wl(B), wr(B); for (int k = 0; k < 16; k++) { W->process(wl.data(), wr.data(), B); ref.insert(ref.end(), wl.begin(), wl.end()); } }
    double md = 0, pk = 0; for (size_t i = 0; i < got.size() && i < ref.size(); i++) { md = std::max(md, (double)std::fabs(got[i] - ref[i])); pk = std::max(pk, (double)std::fabs(ref[i])); }
    std::unique_ptr<Project> now(new Project()); getProject(*now);
    double dev = 0; for (int i = 0; i < NP; i++) dev = std::max(dev, std::fabs(now->tracks[tr].p[i] - t.p[i]));
    double cdev = 0; for (int k = 0; k < NTP; k++) { const int idx = tr * NTP + k; const double want = k < 8 ? srcToHost(t.m, k, t.p[k]) : t.p[TP_IDX[k]]; cdev = std::max(cdev, std::fabs(ctlPlain(idx) - want)); }
    const bool sb = md > 1e-6 * std::max(1.0, pk); soundBad += sb; revertBad += dev > 1e-3; reportBad += (cdev > 1e-3 || repBad);
    std::printf("  %-14s sound diff %.2g (peak %.2f) | project dev %.2g | host shows dev %.2g | %d of %d stale values answered with the plugin's value\n", PRESETS[pi].n, md, pk, dev, cdev, reported, differ);
    worst = std::max(worst, md);
    // reset W's voice history to match the plugin (both played the same note; keep going)
  }
  CHECK(soundBad == 0, "%d sounds differ from the web engine (worst %.3g)", soundBad, worst);
  CHECK(revertBad == 0, "%d sounds reverted to the previous values", revertBad);
  CHECK(reportBad == 0, "%d sounds where the host shows other values", reportBad);

  // 5: output level (UI state "outDb", applied after the limiter): -12 dB scales the output exactly, -80 = silence
  for (double od : { -12.0, -80.0, 0.0 }) {
    const std::string z(1, '\0'); char ui[64]; std::snprintf(ui, sizeof ui, "{\"outDb\":%g}", od);
    Stream st; st.vt = &streamVt; st.data = "__dpf_state_begin__" + z + "ui" + z + ui + z + "__dpf_state_end__" + z + "\xfe";
    v3_cpp_obj(comp)->set_state(comp, (v3_bstream**)&st);
    for (int k = 0; k < 40; k++) process(nullptr);   // let the 15 ms ramp settle
    v3_event e; std::memset(&e, 0, sizeof e); e.type = V3_EVENT_NOTE_ON; e.note_on.pitch = 36; e.note_on.velocity = 110 / 127.f; e.note_on.note_id = -1; ev.ev.push_back(e);
    std::vector<float> got; for (int k = 0; k < 16; k++) process(&got);
    // reference: same engine state, same note
    { std::vector<float> wl(B), wr(B); for (int k = 0; k < 40; k++) W->process(wl.data(), wr.data(), B); }
    W->audition(0, 110, nullptr); std::vector<float> ref; { std::vector<float> wl(B), wr(B); for (int k = 0; k < 16; k++) { W->process(wl.data(), wr.data(), B); ref.insert(ref.end(), wl.begin(), wl.end()); } }
    const double g = od <= -80 ? 0 : std::pow(10.0, od / 20); double md = 0, pk = 0;
    for (size_t i = 0; i < got.size() && i < ref.size(); i++) { md = std::max(md, std::fabs(got[i] - ref[i] * g)); pk = std::max(pk, (double)std::fabs(ref[i])); }
    std::printf("  output level %+.0f dB: max |out - ref x %.4f| = %.2g (ref peak %.2f)\n", od, g, md, pk);
    CHECK(md < 1e-6, "output level %g dB not applied exactly (%.3g)", od, md);
  }

  // 2 + 3: after the edit window, two points at offset 0 -> the last one, and automation is applied
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  { const int idx = tr * NTP + 14; Queue* q = (Queue*)cAdd(&in, &pid[idx], nullptr); q->pts.push_back({ 0, norm(idx, 20) }); q->pts.push_back({ 0, norm(idx, 45) });
    process(nullptr); process(nullptr); std::unique_ptr<Project> now(new Project()); getProject(*now);
    std::printf("  automation T1 Volume, points 20 then 45 at offset 0: project %.2f, host shows %.2f\n", now->tracks[tr].p[21], ctlPlain(idx));
    CHECK(std::fabs(now->tracks[tr].p[21] - 45) < 1e-3 && std::fabs(ctlPlain(idx) - 45) < 1e-3, "automation / last point"); }

  // 6: SAMPLE machine in the host state: samples embedded in the project survive set_state / get_state bit for bit
  //    (also a 20 s sample: a 4 MB state chunk), and the plugin plays them like the reference engine
  {
    auto mk = [](const char* name, int n, int rate, int fmt, uint32_t seed) {
      std::shared_ptr<SampleData> s(new SampleData()); s->name = name; s->rate = rate; s->fmt = fmt; s->d.resize(n); uint32_t x = seed;
      for (int i = 0; i < n; i++) { x = x * 1664525u + 1013904223u; s->d[i] = (float)(0.6 * std::sin(i * 0.02) * std::exp(-i / (0.2 * rate)) + ((x >> 8) / 16777216.0 - 0.5) * 0.2); }
      finishSample(*s); return SampleRef(s); };
    std::unique_ptr<Project> Q(new Project()); *Q = *P;
    Q->smp[3] = mk("kick 24", 30000, 44100, SF_S24, 1); Q->smp[40] = mk("pad f32", 20 * 48000, 48000, SF_F32, 2);
    Q->tracks[0].m = M_SMP; defaultTrackParams(M_SMP, Q->tracks[0].p); Q->tracks[0].p[2] = 3; Q->tracks[0].p[0] = 5;
    Q->tracks[1].m = M_SMP; defaultTrackParams(M_SMP, Q->tracks[1].p); Q->tracks[1].p[2] = 40; Q->tracks[1].p[5] = 2;
    const std::string js = projectToString(*Q);
    CHECK(setStateJson(js) == V3_OK, "set_state with samples");
    for (int k = 0; k < 4; k++) process(nullptr);
    std::unique_ptr<Project> back(new Project()); const bool ok = getProject(*back);
    CHECK(ok && back->smp[3] && back->smp[40] && back->smp[3]->id == Q->smp[3]->id && back->smp[40]->id == Q->smp[40]->id
          && back->smp[3]->d == Q->smp[3]->d && back->smp[40]->d == Q->smp[40]->d, "samples not returned by get_state");
    CHECK(ok && projectToString(*back) == js, "get_state differs from the state that was set (%zu bytes)", js.size());
    // play track 1 and 2 (C1, C#1) and compare with the reference engine given the same project
    for (int k = 0; k < 120; k++) process(nullptr);   // let earlier sounds end
    // the reference with the same history as the plugin (W followed every note so far), given the same project
    Engine* W2 = W.get();
    { std::vector<float> wl(B), wr(B); for (int k = 0; k < 2; k++) W2->process(wl.data(), wr.data(), B); }   // the plugin ran 2 blocks in the automation test
    for (int i = 0; i < NSLOT; i++) W2->setSample(i, Q->smp[i].get()); loadW(*Q);
    { std::vector<float> wl(B), wr(B); for (int k = 0; k < 124; k++) W2->process(wl.data(), wr.data(), B); }
    for (int pitch : { 36, 37 }) { v3_event e; std::memset(&e, 0, sizeof e); e.type = V3_EVENT_NOTE_ON; e.note_on.pitch = (int16_t)pitch; e.note_on.velocity = 110 / 127.f; e.note_on.note_id = -1; ev.ev.push_back(e); }
    std::vector<float> got; for (int k = 0; k < 400; k++) process(&got);
    W2->audition(0, 110, nullptr); W2->audition(1, 110, nullptr);
    std::vector<float> ref; { std::vector<float> wl(B), wr(B); for (int k = 0; k < 400; k++) { W2->process(wl.data(), wr.data(), B); ref.insert(ref.end(), wl.begin(), wl.end()); } }
    double md = 0, pk = 0; for (size_t i = 0; i < got.size() && i < ref.size(); i++) { md = std::max(md, (double)std::fabs(got[i] - ref[i])); pk = std::max(pk, (double)std::fabs(ref[i])); }
    std::printf("  SAMPLE tracks via the host state (%.1f MB): samples returned bit-exact %s; sound vs reference max diff %.2g (peak %.2f)\n", js.size() / 1048576.0, ok ? "yes" : "no", md, pk);
    CHECK(pk > 0.05 && md < 1e-6, "SAMPLE playback differs from the reference engine (%.3g)", md);
  }

  v3_cpp_obj(proc)->set_processing(proc, 0); v3_cpp_obj(comp)->set_active(comp, 0);
  std::printf("%s (%d failures)\n", fails ? "VST3 HOST TEST FAILED" : "VST3 HOST TEST PASSED", fails);
  return fails ? 1 : 0;
}
