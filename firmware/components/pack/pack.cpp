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
  return got > 0;
}

}  // namespace

bool pack_load(const char* root) {
  const std::string base = root;
  bool any = false;

  std::string json;
  if (read_file(base + "/pack.json", json)) {
    std::vector<Mood> m;
    if (packparse::parse_moods(json.c_str(), m) && set_moods(std::move(m))) {
      ESP_LOGI(TAG, "%d moods from pack.json", mood_count());
      any = true;
    } else {
      ESP_LOGW(TAG, "pack.json has no usable moods — keeping built-ins");
    }
  }

  if (read_file(base + "/faces/expressions.json", json)) {
    std::vector<Emotion> e;
    if (packparse::parse_expressions(json.c_str(), e)) {
      if (set_emotions(std::move(e))) {
        ESP_LOGI(TAG, "%d expressions from faces/expressions.json", emotion_count());
        any = true;
      } else {
        // set_emotions() only refuses for one reason worth naming out loud.
        ESP_LOGW(TAG, "expressions have no \"neutral\" — keeping built-ins");
      }
    } else {
      ESP_LOGW(TAG, "faces/expressions.json did not parse — keeping built-ins");
    }
  }

  if (!any) ESP_LOGI(TAG, "no pack data on %s — built-in face and moods", root);
  return any;
}

}  // namespace buddy
