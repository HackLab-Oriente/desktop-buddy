#include "berry_host.h"
#include "bus.h"

#include "esp_log.h"

static const char* TAG = "berry";

#ifndef HAVE_BERRY

namespace buddy {
bool berry_host_start() {
  ESP_LOGW(TAG, "Berry not compiled in (submodule missing) — using C reflexes");
  return false;
}
}  // namespace buddy

#else  // HAVE_BERRY

#include <cstdio>
#include <cstdlib>
#include <string>

#include "berry.h"
#include "esp_timer.h"

namespace buddy {
namespace {

bvm* s_vm = nullptr;
int64_t s_deadline_us = 0;

constexpr size_t kMaxScriptBytes = 32 * 1024;
constexpr size_t kMaxEventName = 64;
constexpr size_t kMaxPayload = 1024;
constexpr int kMaxNesting = 32;
constexpr int64_t kHandlerBudgetUs = 500 * 1000;

// Prefixes a reflex may not publish into. The registry already assigns these
// to firmware and to the web UI; this makes the ownership rule something the
// code enforces rather than something a document asks for. Notably system.* --
// a reflex emitting system.reload is an endless reload loop, which is the
// hazard #55 goes to real trouble to prevent for pack_load.
bool firmware_only(const char* name) {
  static const char* kReserved[] = {"system.", "config.", "boot.", "pack."};
  for (const char* p : kReserved)
    if (strncmp(name, p, strlen(p)) == 0) return true;
  return false;
}

int native_emit(bvm* vm) {
  if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
    const char* name = be_tostring(vm, 1);
    const char* payload = (be_top(vm) >= 2 && be_isstring(vm, 2)) ? be_tostring(vm, 2) : "";
    if (firmware_only(name)) {
      ESP_LOGW(TAG, "reflex may not publish %.*s", (int)kMaxEventName, name);
    } else if (strnlen(name, kMaxEventName + 1) > kMaxEventName) {
      ESP_LOGW(TAG, "event name too long — dropped");
    } else {
      // Truncated rather than dropped: a long say is still a say.
      bus().publish(name, std::string(payload, strnlen(payload, kMaxPayload)));
    }
  }
  be_return_nil(vm);
}

int native_log(bvm* vm) {
  if (be_top(vm) >= 1) ESP_LOGI(TAG, "script: %.256s", be_tostring(vm, 1));
  be_return_nil(vm);
}

// Berry's observability hook fires every ~1M instructions. Without it a
// `while true end` in a reflex hangs the bus task forever: no face, no LEDs,
// and the web UI's reload cannot recover it because the reload runs on the
// task that is hung. Only a power cycle does, and from outside it looks like
// broken hardware. be_raise longjmps to the enclosing be_pcall, which dispatch
// already handles.
void obs_hook(bvm* vm, int event, ...) {
  if (event != BE_OBS_VM_HEARTBEAT) return;
  if (s_deadline_us && esp_timer_get_time() > s_deadline_us) {
    s_deadline_us = 0;
    be_raise(vm, "timeout", "reflex ran too long");
  }
}

// The parser is recursive descent with no depth limit and runs on the bus
// task's 6144-byte stack, at ~270 bytes per nesting level -- so about twenty
// parentheses in an uploaded script overflow it. That is not an error return,
// it is a panic, and since the script persists and is recompiled at every
// boot, it is a boot loop that only reflashing clears.
bool nesting_ok(const std::string& src) {
  int depth = 0;
  bool in_str = false, in_comment = false;
  char quote = 0;
  for (size_t i = 0; i < src.size(); i++) {
    const char c = src[i];
    if (in_comment) { if (c == '\n') in_comment = false; continue; }
    if (in_str) {
      if (c == '\\') i++;
      else if (c == quote) in_str = false;
      continue;
    }
    if (c == '#') in_comment = true;
    else if (c == '"' || c == '\'') { in_str = true; quote = c; }
    else if (c == '(' || c == '[' || c == '{') { if (++depth > kMaxNesting) return false; }
    else if (c == ')' || c == ']' || c == '}') { if (depth > 0) depth--; }
  }
  return true;
}

// Prelude: the `buddy` API surface, in Berry itself. Handlers live in a
// Berry-side list; C only ever calls _dispatch(name, payload).
//
// say goes to speech.say, not face.say: the words the buddy says out loud are
// not the same thing as text on its screen, and they stop being the same event
// the moment voice lands. hint is the screen-only one.
//
// Naming rule, learned the hard way: a verb means the same thing in Berry as
// it does on the bus. `say` used to publish brain.ask while `show` published
// face.say -- so "say" meant utter-these-words at one layer and ask-the-model
// at the other, and whichever you learned first taught you the other one
// backwards. Now: say = these exact words come out; ask = the brain decides
// what comes out. When voice lands, `say` grows to mean screen AND speaker,
// which is already what a reader expects it to mean.
constexpr char kPrelude[] = R"(
_handlers = []
buddy = module('buddy')
buddy.on = def (pattern, fn) _handlers.push([pattern, fn]) end
buddy.emit = def (name, payload) bz_emit(name, str(payload)) end
buddy.led = module('led')
buddy.led.mood = def (m) bz_emit('led.mood', m) end
buddy.face = module('face')
buddy.face.emotion = def (e) bz_emit('face.emotion', e) end
buddy.say = def (text) bz_emit('speech.say', str(text)) end
buddy.hint = def (text) bz_emit('face.say', str(text)) end
buddy.ask = def (prompt) bz_emit('brain.ask', prompt) end
buddy.log = def (msg) bz_log(str(msg)) end

def _match(pattern, name)
  if pattern == '*' return true end
  var n = size(pattern)
  # Compared by index, not by slicing: pattern[n-2..] built a throwaway string
  # for every handler on every event -- 7 allocations and 662 bytes to decide
  # that nothing matches, which was the whole GC load at rest.
  if n >= 2 && pattern[n-1] == '*' && pattern[n-2] == '.'
    var plen = n - 1
    if size(name) < plen return false end
    return name[0..plen-1] == pattern[0..plen-1]
  end
  return pattern == name
end

def _dispatch(name, payload)
  # One map per matching handler, built lazily. Hoisting it out of the loop
  # allocates for events that match nothing -- which is most of them, since the
  # host subscribes to '*' -- and hands one mutable map to every handler.
  for h : _handlers
    if _match(h[0], name)
      h[1]({'name': name, 'payload': payload})
    end
  end
end
)";

std::string read_file(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return "";
  std::string out;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
    if (out.size() + n > kMaxScriptBytes) {
      ESP_LOGW(TAG, "%s is over %d bytes — ignored", path, (int)kMaxScriptBytes);
      fclose(f);
      return "";
    }
    out.append(buf, n);
  }
  fclose(f);
  return out;
}

bool run(const char* code, const char* what) {
  if (be_loadstring(s_vm, code) != 0 || be_pcall(s_vm, 0) != 0) {
    ESP_LOGE(TAG, "%s failed: %s", what, be_tostring(s_vm, -1));
    be_pop(s_vm, be_top(s_vm));
    return false;
  }
  be_pop(s_vm, be_top(s_vm));
  return true;
}

// The script is read and screened BEFORE the running VM is touched. It used
// to be destroyed first, so a script that failed to compile -- or that
// registered two handlers and then threw -- left the reflex layer as a stump,
// silently, with no way back to what was working.
bool load_vm() {
  std::string script = read_file("/flash/reflexes/main.be");
  if (!script.empty() && !nesting_ok(script)) {
    ESP_LOGE(TAG, "main.be nests deeper than %d — refusing to compile", kMaxNesting);
    script.clear();
  }

  bvm* vm = be_vm_new();
  if (!vm) { ESP_LOGE(TAG, "no memory for the VM"); return false; }

  bvm* previous = s_vm;
  s_vm = vm;
  be_set_obs_hook(s_vm, obs_hook);
  be_regfunc(s_vm, "bz_emit", native_emit);
  be_regfunc(s_vm, "bz_log", native_log);

  bool ok = run(kPrelude, "prelude");
  if (ok && !script.empty()) ok = run(script.c_str(), "main.be");

  if (!ok) {
    be_vm_delete(s_vm);
    s_vm = previous;          // keep whatever was working
    ESP_LOGW(TAG, "keeping the previous reflexes");
    return false;
  }
  if (previous) be_vm_delete(previous);
  if (script.empty())
    ESP_LOGW(TAG, "no /flash/reflexes/main.be — upload one via the web UI");
  else
    ESP_LOGI(TAG, "reflexes loaded");
  return true;
}

void dispatch(const Event& ev) {
  if (!s_vm) return;
  be_getglobal(s_vm, "_dispatch");
  if (!be_isfunction(s_vm, -1)) {   // a script can overwrite the global
    be_pop(s_vm, be_top(s_vm));
    return;
  }
  s_deadline_us = esp_timer_get_time() + kHandlerBudgetUs;
  be_pushstring(s_vm, ev.name.c_str());
  be_pushstring(s_vm, ev.payload.c_str());
  if (be_pcall(s_vm, 2) != 0)
    ESP_LOGE(TAG, "dispatch error: %.256s", be_tostring(s_vm, -1));
  s_deadline_us = 0;
  be_pop(s_vm, be_top(s_vm));
}

}  // namespace

bool berry_host_start() {
  // This first load is on the main task; every later one is on the bus task.
  // Safe only because the "*" subscription below does not exist yet.
  load_vm();
  bus().subscribe("system.reload", [](const Event&) { load_vm(); });
  bus().subscribe("*", [](const Event& ev) {
    if (ev.name != "system.reload") dispatch(ev);
  });
  return true;
}

}  // namespace buddy

#endif  // HAVE_BERRY
