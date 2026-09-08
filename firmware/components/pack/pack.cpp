#include "pack.h"

#include <cstdio>
#include <string>
#include <vector>

#include "esp_log.h"
#include "pack_parse.h"

static const char* TAG = "pack";

namespace buddy {
namespace {

// Packs live in a 4 MB partition, but a config file that big is a bug or an
// attack, and read_file() runs before the face is up. 64 KB is generous for
// JSON and small enough to fail fast.
constexpr long kMaxFileBytes = 64 * 1024;

bool read_file(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  const long len = ftell(f);
  if (len <= 0 || len > kMaxFileBytes) {
    if (len > kMaxFileBytes) ESP_LOGW(TAG, "%s is %ld bytes — ignored", path.c_str(), len);
    fclose(f);
    return false;
  }
  rewind(f);
  out.assign(static_cast<size_t>(len), '\0');
  const size_t got = fread(&out[0], 1, static_cast<size_t>(len), f);
  fclose(f);
  out.resize(got);
  // Everything downstream passes c_str() to cJSON, so a NUL in the middle
  // would silently discard the rest of the file -- and the truncated prefix
  // can still parse as valid JSON, which is the worst version of that.
  if (out.find('\0') != std::string::npos) {
    ESP_LOGW(TAG, "%s has an embedded NUL — ignored", path.c_str());
    return false;
  }
  return got > 0;
}

// A relative path from the manifest, screened. The manifest is pack data, so
// "../../flash/reflexes/main.be" is a thing a pack can ask for; only a plain
// relative path under the pack root is allowed.
bool rel_path_ok(const std::string& p) {
  if (p.empty() || p.size() > 128) return false;
  if (p[0] == '/' || p.find("..") != std::string::npos) return false;
  for (const char c : p)
    if (static_cast<unsigned char>(c) < 0x20) return false;
  return true;
}

// The manifest says where the expression map lives. Reading the key rather
// than hardcoding the path matters because packs/zero declares it: a key that
// the loader ignores teaches pack authors something false.
std::string expressions_path(const std::string& json) {
  const std::string fallback = "faces/expressions.json";
  cJSON* root = cJSON_Parse(json.c_str());
  if (!root) return fallback;
  std::string out = fallback;
  const cJSON* ex = cJSON_GetObjectItemCaseSensitive(root, "expressions");
  if (cJSON_IsObject(ex)) {
    const cJSON* map = cJSON_GetObjectItemCaseSensitive(ex, "map");
    if (cJSON_IsString(map) && map->valuestring && rel_path_ok(map->valuestring))
      out = map->valuestring;
  }
  cJSON_Delete(root);
  return out;
}

// set_emotions() refuses a table with no "neutral", but by then it has already
// taken the vector. Checked here so the decision happens before anything is
// applied.
bool has_neutral(const std::vector<Emotion>& v) {
  for (const Emotion& e : v)
    if (e.name == "neutral") return true;
  return false;
}

}  // namespace

bool pack_load(const char* root) {
  const std::string base = root;

  // Parse BOTH files before applying EITHER, so the decision about one table
  // is taken knowing what the other one turned out to be. Ordering alone does
  // NOT make the install atomic -- either table can still go in on its own,
  // and a pack with no `moods` depends on exactly that. What keeps the pair in
  // step is the reconciliation further down, once it is known which two tables
  // will actually be active.
  std::string json;
  std::string emo_rel = "faces/expressions.json";
  std::vector<Mood> moods_new;
  bool have_moods = false;
  if (read_file(base + "/pack.json", json)) {
    emo_rel = expressions_path(json);
    // bare_map = false: this is the manifest. Read as a bare mood map, every
    // top-level key ("id", "name", "expressions") becomes a mood.
    if (packparse::parse_moods(json.c_str(), moods_new, /*bare_map=*/false)) {
      have_moods = true;
    } else {
      ESP_LOGW(TAG, "pack.json has no usable moods — keeping built-ins");
    }
  }

  std::vector<Emotion> emos_new;
  bool have_emos = false;
  if (read_file(base + "/" + emo_rel, json)) {
    if (!packparse::parse_expressions(json.c_str(), emos_new)) {
      ESP_LOGW(TAG, "%s did not parse — keeping built-ins", emo_rel.c_str());
    } else if (!has_neutral(emos_new)) {
      // The one refusal worth naming out loud: the renderer starts at neutral.
      ESP_LOGW(TAG, "expressions have no \"neutral\" — keeping built-ins");
    } else {
      have_emos = true;
    }
  }

  if (!have_moods && !have_emos) {
    ESP_LOGI(TAG, "no pack data on %s — built-in face and moods", root);
    return false;
  }

  for (const Mood& m : moods_new)
    if (m.unknown_dir)
      ESP_LOGW(TAG, "mood \"%s\": dir is neither cw nor ccw — spinning cw",
               m.name.c_str());

  // Applying only one of the two tables is legal -- a pack with no `moods`
  // keeps the built-in four by design -- but the pair still has to agree. It
  // did not: the expressions that end up active can name moods from the table
  // that did NOT end up active. Today the names happen to coincide, so nothing
  // shows; a pack that names its moods in its own language turns every face
  // change into a warning and freezes the ring. Reconcile the pair that is
  // about to be installed, not the one the pack asked for.
  std::vector<Mood> moods_floor;
  if (!have_moods) moods_floor.assign(moods(), moods() + mood_count());
  const std::vector<Mood>& moods_active = have_moods ? moods_new : moods_floor;

  std::vector<Emotion> emos_floor;
  if (!have_emos) emos_floor.assign(emotions(), emotions() + emotion_count());
  std::vector<Emotion>& emos_active = have_emos ? emos_new : emos_floor;

  std::string orphans;
  const int dropped =
      static_cast<int>(packparse::drop_orphan_moods(emos_active, moods_active, &orphans));
  if (dropped)
    ESP_LOGW(TAG, "%d expression(s) name a mood that is not in the active table"
                  " (%s) — they no longer set one",
             dropped, orphans.c_str());

  bool any = false;
  if (have_moods && set_moods(std::move(moods_new))) {
    ESP_LOGI(TAG, "%d moods from pack.json", mood_count());
    any = true;
  }
  // The built-in expressions are reinstalled only when a name was dropped from
  // them; untouched, they are already in place.
  if (have_emos || dropped) {
    const bool applied = set_emotions(std::move(emos_active));
    if (applied && have_emos) {
      ESP_LOGI(TAG, "%d expressions from %s", emotion_count(), emo_rel.c_str());
      any = true;
    }
  }
  if (!any) ESP_LOGW(TAG, "tables are frozen — pack_load() ran too late");
  return any;
}

}  // namespace buddy
