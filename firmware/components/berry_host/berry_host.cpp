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

namespace buddy {
namespace {

bvm* s_vm = nullptr;

// Native: bz_emit(name, payload) → bus
int native_emit(bvm* vm) {
  if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
    const char* name = be_tostring(vm, 1);
    const char* payload = (be_top(vm) >= 2 && be_isstring(vm, 2)) ? be_tostring(vm, 2) : "";
    bus().publish(name, payload);
  }
  be_return_nil(vm);
}

// Native: bz_log(msg)
int native_log(bvm* vm) {
  if (be_top(vm) >= 1) ESP_LOGI(TAG, "script: %s", be_tostring(vm, 1));
  be_return_nil(vm);
}

// Prelude: the `buddy` API surface, in Berry itself. Handlers live in a
// Berry-side list; C only ever calls _dispatch(name, payload).
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
buddy.say = def (text) bz_emit('face.say', str(text)) end
buddy.ask = def (prompt) bz_emit('brain.ask', prompt) end
buddy.log = def (msg) bz_log(str(msg)) end

def _match(pattern, name)
  if pattern == '*' return true end
  var n = size(pattern)
  if n >= 2 && pattern[n-2..] == '.*'
    var prefix = pattern[0..n-2]
    return size(name) >= size(prefix) && name[0..size(prefix)-1] == prefix
  end
  return pattern == name
end

def _dispatch(name, payload)
  for h : _handlers
    if _match(h[0], name) h[1]({'name': name, 'payload': payload}) end
  end
end
)";

std::string read_file(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return "";
  std::string out;
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
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

void load_vm() {
  if (s_vm) be_vm_delete(s_vm);
  s_vm = be_vm_new();
  be_regfunc(s_vm, "bz_emit", native_emit);
  be_regfunc(s_vm, "bz_log", native_log);
  run(kPrelude, "prelude");

  std::string script = read_file("/flash/reflexes/main.be");
  if (script.empty()) {
    ESP_LOGW(TAG, "no /flash/reflexes/main.be — upload one via the web UI");
    return;
  }
  if (run(script.c_str(), "main.be")) ESP_LOGI(TAG, "reflexes loaded");
}

void dispatch(const Event& ev) {
  if (!s_vm) return;
  be_getglobal(s_vm, "_dispatch");
  be_pushstring(s_vm, ev.name.c_str());
  be_pushstring(s_vm, ev.payload.c_str());
  if (be_pcall(s_vm, 2) != 0)
    ESP_LOGE(TAG, "dispatch error: %s", be_tostring(s_vm, -1));
  be_pop(s_vm, be_top(s_vm));
}

}  // namespace

bool berry_host_start() {
  load_vm();
  // Both subscriptions run on the bus task — the VM's single home thread.
  bus().subscribe("system.reload", [](const Event&) { load_vm(); });
  bus().subscribe("*", [](const Event& ev) {
    if (ev.name != "system.reload") dispatch(ev);
  });
  return true;
}

}  // namespace buddy

#endif  // HAVE_BERRY
