#pragma once
// Loads the active pack's data tables off flash.
//
// The flash root IS the active pack: /flash/pack.json, /flash/faces/…,
// /flash/reflexes/main.be. That mirrors where the Berry host already looks,
// and it is the layout the build flashes from packs/zero. Selecting between
// several installed packs is #21; this loads the one that is there.
//
// Never fatal. A missing, unreadable or malformed pack leaves the built-in
// tables in place and the buddy boots with a face.
namespace buddy {

// Reads <root>/pack.json (moods) and <root>/faces/expressions.json.
// Returns true if either replaced its built-in table.
bool pack_load(const char* root = "/flash");

}  // namespace buddy
