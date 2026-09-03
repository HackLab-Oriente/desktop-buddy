#pragma once
// Berry build configuration for the buddy — a PACK SANDBOX, not a desktop REPL.
//
// This file shadows berry/default/berry_conf.h, which is upstream's reference
// configuration for a desktop interpreter. It shipped onto the device
// unmodified, so a reflex had the OS module, the file system, introspection
// and a bytecode loader. Reflexes arrive over HTTP and inside shared packs.
//
// It lives here and not in the submodule because an edit there is erased by
// the next dependabot bump.
//
// If you change anything below, the coc codegen must be re-run against THIS
// file — the constant tables are generated per configuration. See
// .github/workflows/build.yml and the comment in ../CMakeLists.txt.

#include_next "berry_conf.h"

// --- What a reflex may reach ------------------------------------------------
//
// The prelude uses none of these. Each one was measured against the API
// surface in docs/pack-format.md before being turned off.

// introspect.fromptr(n) casts an arbitrary integer to a heap object and hands
// it back live: arbitrary read/write anywhere in the address space, from a
// text file, on a chip with no MMU.
#undef BE_USE_INTROSPECT_MODULE
#define BE_USE_INTROSPECT_MODULE 0

// The bytecode loader reads an attacker-chosen 32-bit count and multiplies it
// into an allocation, then trusts every opcode and the register-window sizes.
// There is no verifier. `import` also tries .bec BEFORE .be, so leaving this
// on means any future module path prefers unvalidated bytecode over source.
#undef BE_USE_BYTECODE_LOADER
#define BE_USE_BYTECODE_LOADER 0
#undef BE_USE_BYTECODE_SAVER
#define BE_USE_BYTECODE_SAVER 0

// open(), os.remove, os.listdir, os.chdir. Unrooted, so one pack can read and
// rewrite another pack's files -- installing a cartridge would edit the
// cartridges you already had. Re-enable behind a path root in be_port.c, not
// before.
#undef BE_USE_FILE_SYSTEM
#define BE_USE_FILE_SYSTEM 0
#undef BE_USE_OS_MODULE
#define BE_USE_OS_MODULE 0

// global.setmember/undef can replace bz_emit itself.
#undef BE_USE_GLOBAL_MODULE
#define BE_USE_GLOBAL_MODULE 0

// Host-side development tools with no runtime use on the device.
#undef BE_USE_SOLIDIFY_MODULE
#define BE_USE_SOLIDIFY_MODULE 0
#undef BE_USE_DEBUG_MODULE
#define BE_USE_DEBUG_MODULE 0

// One function returning a fresh copy of the module path. Inert; costs flash.
#undef BE_USE_SYS_MODULE
#define BE_USE_SYS_MODULE 0

// __POSIX_OS__ is never defined on xtensa, so this is dead code that emits a
// #warning on every build.
#undef BE_USE_SHARED_LIB
#define BE_USE_SHARED_LIB 0

// --- Sizing -----------------------------------------------------------------

// Upstream's 20000 is a desktop number: 20000 * sizeof(bvalue) is ~234 KB on
// Xtensa, so a runaway recursion in a pack exhausts the heap before Berry
// notices. 2000 is what Tasmota runs on the same silicon.
#undef BE_STACK_TOTAL_MAX
#define BE_STACK_TOTAL_MAX 2000
