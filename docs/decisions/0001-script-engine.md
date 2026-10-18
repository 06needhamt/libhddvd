# 0001: QuickJS as the script runtime's first engine

**Status:** accepted (P4-03)

## Context

HDi scripts are ECMAScript 3 source in UTF-16BE (`docs/formats/hdi-script.md`).
libhddvd must run them in a sandbox with heap, recursion, instruction and
wall-clock budgets, without tying its public API, or any build, to a
particular engine (`CLAUDE.md`: libhddvd stays independent of any JavaScript
engine; libxml2 is the only required dependency).

## Options considered

| Engine | Language | Budgets available without patching | Notes |
|---|---|---|---|
| QuickJS (Bellard), 2021-03-27 to 2026-06-04 | ES2023 superset of ES3 | heap (custom allocator), native stack, interrupt handler for operations and time | MIT; small C99-ish source; no MSVC support |
| Duktape 2.7 as packaged by Ubuntu | ES5.1 | heap only: `DUK_USE_EXEC_TIMEOUT_CHECK` and the interrupt counter are compiled out | MIT; would need a custom build for time and operation budgets |
| QuickJS-ng | ES2023+ | as QuickJS | different API and install layout from Bellard's QuickJS |
| A libhddvd interpreter | ES3 | any | far more work, and a second ECMAScript implementation to keep correct |

## Decision

QuickJS, behind an internal interface (`src/script_engine.h`) that only
`src/runtime.c` and `src/runtime_dom.c` use. The public API (`hddvd_runtime_*`)
names no engine type. Builds pick the backend at configure time with
`HDDVD_SCRIPT_ENGINE` (`auto`, `quickjs`, `none`); `src/script_none.c` makes
every create fail with `HDDVD_ENOTSUP` and leaves
`HDDVD_FEATURE_SCRIPT_RUNTIME` unset. Another engine would be a new
`src/script_<engine>.c` implementing the same seven functions.

The HDi host objects (P4-04) are written in JavaScript
(`src/hdi_prelude.js`) rather than against an engine's object API, so they
move to another engine unchanged. A backend only has to install the prelude
with a table of native functions, passing plain values (undefined, null,
booleans, numbers and strings), and call its entry functions.

## License and packaging

- QuickJS is MIT-licensed; nothing of it is committed to this repository.
- **Source mode** (`HDDVD_QUICKJS_SOURCE_DIR`): a QuickJS release is compiled
  into libhddvd as position-independent code with hidden symbols. The shared
  library exports only `hddvd_*`, and the installed package needs nothing
  extra beyond libm and pthreads for static consumers. CI uses
  `tools/fetch_quickjs.sh`, which pins 2026-06-04 and checks its SHA-256.
- **Installed mode:** an installed `libquickjs.a` is linked privately, with its
  symbols excluded from the shared library's exports. Distribution packages,
  including Ubuntu's 2021-03-27, are not built with `-fPIC`; configure detects
  this and only static-only builds can use them.
- A static libhddvd built in source mode contains QuickJS's `JS_*` symbols, so
  a program that also links its own QuickJS should use the shared library.
- QuickJS does not build with MSVC, so Windows builds have no runtime.

## Sandbox

The context is created with `JS_NewContextRaw` and only the base objects,
`Date`, `eval`, `RegExp` and `JSON`. QuickJS's `std` and `os` modules, module
loading, `Promise`, `Proxy`, typed arrays and `BigInt` are not added, so
scripts have no file, network, process or timer access of their own.

## Budgets

| Budget | Mechanism | Precision |
|---|---|---|
| Heap | libhddvd's allocator for the runtime, counting every block and its header | Exact. An allocation refused by the budget is reported even if the script catches the error. |
| Recursion | `JS_SetMaxStackSize(max_recursion × HDDVD_RUNTIME_FRAME_BYTES)` with the stack top reset at each entry | By native stack, not call count. 16 KiB per level is at least 4.6 times the largest optimized frame measured (see below). A script catching the overflow error continues. |
| Operations | Interrupt handler, which QuickJS calls every 10,000 backward jumps and calls | To within one block of 10,000 |
| Time | The same handler compares a monotonic clock with the entry's start | Checked at the same points; a single long native operation, such as a pathological regular expression, is not interrupted |

Operation and time budgets cannot be caught by scripts (QuickJS marks the
error uncatchable). After any budget stops a script, the runtime remains
usable.

Native stack per recursion level depends heavily on how QuickJS is compiled:

| QuickJS build | Plain recursion | Recursion through a native callback (`Array.prototype.map`) |
|---|---|---|
| GCC `-O2` | 0.75 KB | 2.6 KB |
| Clang `-O2` | 1.0 KB | 3.4 KB |
| GCC `-O0` | 3.1 KB | 10.3 KB |
| Clang `-O0` | 10.5 KB | 33.8 KB |
| Clang `-O0` with ASan | 75 KB | 262 KB |

So in source mode QuickJS's objects are always compiled with `-O2` and
`-fno-sanitize=all`, whatever libhddvd's build type and sanitizers;
libhddvd's side of the boundary keeps them. An installed QuickJS is assumed to
be an optimized build, as distribution packages are.

## Determinism

- `Math.random` is replaced by xorshift64* with a fixed seed, so every runtime
  produces the same sequence.
- `Date` still reads the system clock. The player host objects (P4-05) should
  supply the title clock instead.
- The recursion depth at which a script fails depends on the build, because it
  is measured in native stack.

## Fallback

If QuickJS proved unsuitable, for example for a semantic difference found by
the reference-script work (P4-06), a different engine replaces
`script_quickjs.c` without changing the public API. Builds without any engine
keep working and report the missing capability.

## Validation

- `test_runtime.c`, `test_limits.c`, `test_threads.c` and `fuzz-runtime` cover
  the API, encodings, every budget and concurrent runtimes.
- They pass with QuickJS 2021-03-27 and 2026-06-04 from source, with Ubuntu's
  2021-03-27 package in a static-only build, without an engine, under
  ASan+UBSan and ThreadSanitizer, and in a 32-bit build.
- All 126 readable scripts from the ripped discs compile without syntax
  errors. 57 run their top-level code to completion. The other 69 stop on a
  name that is not defined: either an HDi host object, or a helper from
  another script of the same application, since each was run alone.
