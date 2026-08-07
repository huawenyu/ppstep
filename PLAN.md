# ppstep — improvement plan / log

This file documents the direction and the work that has landed
in four incremental phases. Goal: turn `ppstep` from a stepping
debugger into a gdb-shaped macro-expansion teaching tool.

## Direction

`ppstep` is a C/C++ preprocessor single-step debugger built on
Boost.Wave. The CLI is REPL-shaped already (`step`, `continue`,
`break`, `delete`, `info`, `expand`, `list`, `help`). The gaps
that matter most for *teaching* the preprocessor:

1. **No per-token-type coloring.** Tokens all share one default
   fg; only the diff range gets a background. Hard to tell at a
   glance which token is a keyword vs. identifier vs. literal.
2. **No source-context display.** Each step prints a token line;
   nothing anchors it to a file location or shows the macro body
   being substituted.
3. **No "step out of this expansion".** `continue` runs to a
   breakpoint; there is no `finish` to skip past a single
   expansion cleanly (especially when nested macros share the
   same name).
4. **No introspection at the call site.** When stopped at a
   `call`, the user has no way to see `param ← token-list`
   bindings.

Phases 1–4 land fixes for those four, in order.

---

## Phase 1 — per-token-type coloring

**What ships**: `set color always|auto|never` toggles the fg
coloring. When `always` (or `auto` on a TTY), each token is
colored by its Boost.Wave category:

| Category        | Color                                |
|-----------------|--------------------------------------|
| Keyword (`int`, `if`, …)         | `\e[36;1m` cyan-bold |
| Identifier (`MAX`, `area`, …)    | `\e[37;1m` white-bold |
| Number / char / bool literal     | `\e[35m` magenta        |
| String literal                   | `\e[32m` green          |
| Operator / punctuator            | `\e[33m` yellow         |

Background highlights (the existing diff bg) are gated on the
same flag, so `set color never` produces byte-identical plain
text. Default `auto` is decided at startup via `isatty(STDOUT)`.

**Files**: `src/utils.hpp` (helpers + `g_color_enabled`),
`src/client.hpp` (gate bg highlights on flag), `src/view.hpp`
(grammar rule `set color …`, tab completion, help text),
`src/ppstep.cpp` (TTY init for `auto`).

---

## Phase 2 — verbose per-event panel

**What ships**: `set verbose on|off`. When on, each stop renders
a 3-line source context (`>` marks current line) above the
existing colored event line, plus one extra annotation:

| Event       | Annotation                                  |
|-------------|---------------------------------------------|
| `call NAME` | `── definition: #define NAME(params) body`   |
| `expanded`  | `── expanded from: <initial tokens>`        |
| `rescanned` | `── rescan caused by: <cause>  (over: <initial>)` |
| `lexed`     | (no extra — single token)                   |

The macro-definition lookup is live: it calls
`ctx.get_macro_definition(name, …)` to read the params and body
at the time of the stop. Combines cleanly with `set color`.

**Files**: `src/client.hpp` (added `verbose` flag, `set_verbose()`,
`print_verbose(os, tokens, ctx)` member fns on each event
struct), `src/view.hpp` (factored out `print_source_context`,
branched `current_state` to use `print_verbose`).

---

## Phase 3 — `finish` (run until current expansion completes)

**What ships**: `finish` / `fin`. Runs the preprocessor until
the expansion we are inside completes (i.e., the matching
`expanded` event that pops back to the caller's depth).

The non-obvious bit: nested calls of the *same* macro name (e.g.
`MAX(MAX(...), 3)`) would match a naive `expanded MAX`
breakpoint on the *inner* expansion, not the outer. `finish`
records the current `state->expanding` depth at invocation
time, and the next `expanded` event at *exactly* that depth
(not strictly less) is the one that pops back to the caller.

To make the depth visible at the `call` event, server.hpp was
reordered to push the call's tokens onto `state->expanding`
*before* invoking `sink->on_expand_function` (previously the
push happened after the prompt returned).

**Behavior**:

```
(pp) b call MAX
(pp) c
[macro_demo.c:7:15]: ... int biggest = MAX ( MAX ( 1 , 2 ) , 3 )
(pp) finish
Finishing expansion of: MAX  (depth 1)
[macro_demo.c:7:15]: ... int biggest = ( ( ( ( 1 ) > ( 2 ) ? ... ) ) > ( 3 ) ? ... : ( 3 ) )
```

The inner `MAX(1, 2)` was already substituted by the time
`expanded` fires for the outer call, so `finish` lands on a
clean post-rescan state.

**Files**: `src/client.hpp` (`arm_finish`, `finish_target_depth`,
`finish_pending` flag; depth check first inside the `EXPANDED`
case of `handle_prompt`), `src/server.hpp` (push state before
callback), `src/view.hpp` (`finish()` action + grammar + help).

---

## Phase 4 — `info args` (param ← tokens at current call)

**What ships**: `info args` / `i a`. At a `call` event, looks
up the macro's formal parameters via the wave context and
prints the argument tokens that were passed at this call site:

```
(pp) i a
args of MAX:
  a ← MAX ( 1 , 2 )
  b ← 3
```

Object-like macros print `PI is object-like — no arguments.`
Non-call events print a one-line "no args" message. Each event
struct now owns a `print_args(os, ctx)` method so dispatch is
plain `std::visit` with no `if constexpr`-on-events hack (the
events namespace isn't visible from `view.hpp` without a
circular include, so per-event polymorphism is the cleaner
shape).

**Files**: `src/client.hpp` (`events::call` now stores
`std::vector<ContainerT> arguments`; default-constructed to
`{}` for object-like; `print_args` on every event),
`src/view.hpp` (`show_args(ctx)`, grammar rule, completion, help).

---

## What's next (candidates)

1. **`info macro NAME`** — single-macro introspection (already
   partly used by `print_verbose`/`print_args`; promote to a
   direct command and avoid the lookup overhead at every stop).
2. **`watch TOKEN`** — break when any token whose value
   matches `TOKEN` is seen. Currently bp matches only by macro
   name; this would match the actual text in the stream.
3. **`condition N EXPR`** — add a guard predicate to an
   existing breakpoint id.
4. **`ignore N K`** — skip next K hits of bp #N.
5. **Reverse step / `history N`** — revisit an earlier step
   from `token_history`. The README's TODO of "reverse stepping
   to rewind preprocessing" can be partially satisfied by
   re-displaying a stored historical state without re-running
   the preprocessor.
6. **Refactor** — split `client.hpp` (~600 lines) and
   `view.hpp` (~620 lines) into `.cpp` + `.hpp` units to cut
   incremental rebuild times. Both headers are included from
   almost every TU today, and GLOB_RECURSE means each
   `utils.hpp` change rebuilds the world.
7. **Tests + CI** — there's no test suite and no
   `.github/workflows/`. A small set of golden-output checks
   (run `ppstep` on a macro-heavy file, pipe `q`, diff stdout)
   + a CMake/CTest target would catch regressions in the REPL
   grammar and event formatting without a full integration rig.

### Phase 4½ — bare `info` summary (landed after this doc was
first written)

Bare `info` (no subcommand) now prints an expanding-context
summary: the root macro in the call stack and the current
expanding one, each with their `#define` details. Grammar rule
`((lit("info") | lit("i")) >> eoi)[show_info_summary]` placed
right before the existing `info <subcmd>` rule so `info args`
etc. still match. Implementation in `src/view.hpp`,
`show_info_summary(ctx)` method.

---

## Brainstorm — `info` extensions worth shipping

Each addresses a real "what just happened in the preprocessor?"
question that's currently invisible.

1. **`info stack`** (or extend `bt` to `bt full`) — every
   expansion frame with **bound args** at each level. Currently
   `bt` shows the call tokens, not the bindings. Frame #0 has
   `a=MAX(1,2), b=3`; frame #1 has `x=2.0`, etc. This is the
   gdb `bt full` analogue and probably the single
   highest-leverage addition. Requires storing arguments
   alongside each `state->expanding` frame instead of carrying
   them only on the `events::call` short-lived struct.

2. **`info macro NAME`** — already partially used internally
   (`print_verbose`, `print_args`); promote to a first-class
   command. Look up the macro by name and pretty-print (params,
   body, where it's called from, expansion depth). Useful when
   stepping in `expand MACRO` sub-prompts where the
   `state->expanding` is empty and the user would otherwise have
   no context.

3. **`info count`** — running counters: total events,
   per-event-type totals, per-macro call totals. Cheap (one
   line), useful for "why is this so slow?" or "did my breakpoint
   fire twice or three times?". Just bumps atomic counters in
   each event handler.

4. **`info where TOKEN`** — for an identifier or literal
   currently in the preprocessor's output, print the source
   position (`file:line:col`) it came from.
   `boost::wave::lex_token::get_position()` carries this data
   already; the lookup is a `find_if` over `token_history` (or
   a per-token annotation cache). Bridges the gap between "I see
   `((1) > (2) ? (1) : (2))` here" and "it came from
   macro_demo.c:7:13".

5. **`info this`** — single-frame context (current macro name,
   definition, bound args, depth, source line, next predicted
   event). Same shape as the `set verbose` per-event panel, but
   only the current frame, on demand. Cheap, complements the
   verbose-mode auto-print.

6. **`info rescan`** — show the queue of tokens about to be
   rescanned, plus which macro's body produced them and how deep
   the rescan stack currently is. The existing `forwardtrace`
   shows the *future* work but just as a token list;
   `info rescan` could give a richer view with body context for
   each pending rescan frame.

7. **`info path TOKEN`** — the gdb `trace` analogue. Given an
   identifier in the current expanded output, walk the chain of
   macro expansions that produced it. Reads `token_history`
   backwards to find every `call`/`expanded` event that touched
   this token. Strongest teaching feature but the heaviest to
   implement — the token-position-time correspondence has to be
   recorded during preprocessing, which is currently thrown
   away after each event handler returns.

### Recommended priority

If continuing work after this doc:

- **`info stack`** if maximum gdb-shape coverage in one shot.
- **`info macro NAME`** for quick introspection that's already
  half-built.
- **`info count`** if a low-cost, high-utility stat is wanted.
- **`info path TOKEN`** as a long-term centerpiece teaching
  feature.

---

## Handoff

Pickup notes for whoever continues work on this codebase after
me. The repo is a Boost.Wave-backed preprocessor debugger with
its own REPL; the recent additions (Phases 1–4 above) live in
five header files — there is no `.cpp`, just headers.

### Build & smoke-test

```sh
cd /ssd/proj/ppstep
mkdir -p build && cd build
cmake -DCMAKE_C_COMPILER=/usr/bin/gcc \
      -DCMAKE_CXX_COMPILER=/usr/bin/g++ ..
make -j4
```

CMake uses `file(GLOB_RECURSE)` for `src/` and `external/`, so
new `.hpp` files are picked up automatically — no re-running
cmake needed for additions, only deletions require a re-glob.

LSP false positives you can ignore: clangd's analyzer cannot
find `linenoise/linenoise.h` and reports
`linenoiseHistorySetMaxLen` etc. as undeclared — the cmake
target compiles fine because
`target_include_directories(ppstep PUBLIC src external)` covers
`external/linenoise/`.

### Layout

| File                 | What's in it |
|----------------------|--------------|
| `src/ppstep.cpp`     | `main()`; TTY init for `g_color_enabled` (auto). |
| `src/client_fwd.hpp` | Forward decls + the `preprocessing_event_type` enum. |
| `src/server_fwd.hpp` | Forward decls for `server`/`server_state`. |
| `src/client.hpp`     | The `client<TokenT, ContainerT>` class, the `events::*` event structs that go in `std::variant<preprocessing_event>`, and `client_cli<TokenT, ContainerT>`. (Note the unusual include direction: `client.hpp` includes `view.hpp`, not the reverse.) |
| `src/server.hpp`     | The Boost.Wave hooks (`expanding_function_like_macro` etc.). |
| `src/view.hpp`       | REPL grammar, `client_cli` impl, linenoise init, completion callback. Splitting this into a `.cpp` is overdue. |
| `src/utils.hpp`      | Printing helpers and `g_color_enabled`. |

### Key invariants established by Phases 1–4

1. **`g_color_enabled` is a process-global bool** in
   `utils.hpp`, set by:
   - `set_color_enabled(bool)` (called from `ppstep.cpp` for
     the TTY-detect default and from `view.hpp`'s `set color`)
   - When off, `print_token` produces no ANSI; when on, it
     picks a fg color via `token_fg_color_for()` (which uses
     `boost::wave::IS_CATEGORY` first, then `token_id()`).
2. **Background highlights also gated on `g_color_enabled`**
   — every `formatting_event::format()` method returns early
   when color is off, so `set color never` is byte-identical to
   the pre-Phase-1 output.
3. **Server pushes `state->expanding` *before* invoking the
   sink.** This was a Phase 3 change; it lets the `call` prompt
   reflect the new depth. If you add a new hook that pushes to
   `state->expanding`, do push-first.
4. **`finish` is depth-based, not bp-on-name.**
   `finish_target_depth` is recorded; the next `expanded` event
   at exactly that depth fires. Nested same-name macros
   (e.g. `MAX(MAX(1,2),3)`) work without spuriously hitting
   inner expansions.
5. **`events::call` carries actual argument tokens.** Object-like
   calls get `{}` via the default in the constructor. Arg lookup
   for `print_args` is via the wave context's
   `get_macro_definition`.
6. **Per-event polymorphism**: `view.hpp` cannot reference
   `events::call<ContainerT>` because the `events` namespace is
   defined in `client.hpp`, which `view.hpp` does not (and
   must not) include. So every event carries its own
   `print_args(os, ctx)` method, and `view.hpp::show_args`
   just calls `event.print_args(...)` after `std::visit`.

### Known quirks / things that bit me

* **Spirit grammar alternation order matters.** The earlier
  attempt to add a `set` rule placed it after `step`/`continue`
  in the chain. `lit("s")` from the `step` alias matched just
  the `s` of `set`, succeeded partially, and `parse()` then
  reported "Undefined command: set". Putting the `set` rule
  *first* fixed it. If a new command's first word shares a
  prefix with a single-letter alias, put it earlier.

* **`>` vs `>>` in the grammar**. `>` commits (no backtrack);
  `>>` does not. Inside alternative groups with action
  branches, `>` is often what you want; for sequential chains,
  `>>` is fine.

* **`lit("d 1")` doesn't parse the `d N` form reliably.** The
  first alternative `(delete | d) *space > ((call|c) +space
  anything …)` commits via `>` and even though it ultimately
  fails on `1`, the parser doesn't try the second alternative
  `(delete | d) +space + uint_`. Same partial-match pattern
  as the `set` issue. Currently no fix has been applied to
  the user-facing grammar; `d 1` may say "Undefined command",
  and the workaround is to use a different flow (e.g.
  `d call MAX` after hitting the bp, or just `d` first to clear
  all then re-set). Worth fixing once the grammar gets
  refactored.

* **The awk reindent that was used to repair `parse()`** in
  Phase 2 left indentation one level deeper than the original
  author's style. Indentation in `view.hpp`'s `parse()`
  function body is 16 spaces instead of 12. Functionality
  unaffected.

* **Build directory lives at `/ssd/proj/ppstep/build`** but
  the cmake-generated cache was originally created under
  `/home/hyu/proj/ppstep/build` — the build still works
  because it's a relative path. If you blow away `build/`
  with `rm -rf build`, regenerate with `mkdir build && cd
  build && cmake ..`.

### How to test changes without a test suite

There's no `tests/` directory. The "test rig" is:

```sh
# Pipe commands in, capture output
printf 'set color never\nb call MAX\nc\ni a\nq\n' \
  | ./build/ppstep /tmp/claude/macro_demo.c
```

Sample test file is at `/tmp/claude/macro_demo.c` (created
during Phase 1 work):

```c
#define PI 3.14159
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
double area = PI * SQUARE(2.0);
int biggest = MAX(MAX(1, 2), 3);
```

Useful pipeline primitives:
* `set color always|never|auto` — toggle coloring before
  stepping.
* `set verbose on|off` — toggle source-context panels.
* `b call NAME` / `d call NAME` — break / delete on call
  events (a stable approach; the `d N` form is iffy, see
  above).
* `finish` — skip to next outer `expanded`.
* `step` — fire one event.
* `q` / `quit` — exit.

A nice first CI addition would be a golden-output test that
hashes the stdout from a fixed command sequence on
`/tmp/claude/macro_demo.c`.

### Open questions / risks for future phases

* **`info macro NAME`** — Needs to use a similar
  `ctx.get_macro_definition` lookup to `print_args`. Make
  sure to handle "macro not found" cleanly.
* **`watch TOKEN`** — The `token.get_value()` lookup has to
  happen against every event's tokens. Easy for `call`
  (carry a `std::set` of watched strings), but for
  `expanded`/`rescanned` the "currently-being-scanned"
  tokens aren't directly accessible — they're in the
  spliced result. May need to thread another hook callback
  into the existing `token_history` rather than the bp
  mechanism.
* **Reverse step** — `token_history` already records each
  event with its `historical_event::tokens`. Reading back
  a prior state is straightforward; the harder bit is
  re-feeding the preprocessor from that point.
* **Refactor** — `view.hpp`'s `parse()` template and the
  `client_fwd.hpp`-`client.hpp`-`server.hpp` inclusion
  cycle make splitting painful. The minimal first step is
  to move the linenoise init + completion callback out of
  `view.hpp` into a separate `linenoise_setup.{hpp,cpp}`
  with no project dependencies — that's the leaf.

