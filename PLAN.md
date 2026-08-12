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

## Phase 5 — `info stack` / `bt full` + `info macro NAME` + quit crash fix

Three pieces landed together.

### `info stack` / `bt full` — per-frame bound args (the gdb `bt full` analogue)

**What ships**: `info stack` (`i s`) and `bt full` print every
expansion frame with its bound `param ← arg` tokens at each level,
not just the call tokens. Frame #0 is the innermost
(currently-expanding) call.

```
(pp) b call MAX
(pp) c
(pp) info stack
Expansion stack (depth 2, innermost first):
#0  MAX  <- current
    a ← 1
    b ← 2
#1  MAX
    a ← MAX ( 1 , 2 )
    b ← 3
```

`bt full` is the same as `info stack` but reached via the
`backtrace` verb. `bt N full` limits to N frames.

**How**: `server_state::expanding` changed from
`std::vector<ContainerT>` to
`std::vector<expansion_frame<ContainerT>>`, where
`expansion_frame` holds both the `call` tokens and the
`std::vector<ContainerT> arguments` bound at that call. Arguments
were previously carried only on the short-lived `events::call`
struct (gone after the prompt returned); now they live for the
lifetime of the frame on the stack. `server.hpp` pushes the
sanitized arguments into the frame alongside the call tokens at
`expanding_function_like_macro` / `expanding_object_like_macro`
time. Every existing call site that read `frame.begin()->get_value()`
was updated to `frame.call.begin()->get_value()`.

The `print_frame_args` helper (in `view.hpp`) looks up the macro's
formal parameters via `ctx.get_macro_definition` and pairs them with
the frame's argument token-lists, so the labels match the
definition. Shared by `bt full` and `info stack`.

**Files**: `src/server.hpp` (`expansion_frame` struct,
`server_state::expanding` retype, push sites carry args,
`expanded_macro` reads `.call`), `src/server_fwd.hpp`
(forward-decl `expansion_frame`), `src/client.hpp` (banner /
frames-pane sites read `.call`), `src/view.hpp`
(`expanding_trace(ctx, limit, full)` template,
`print_frame_args`, `show_stack`, grammar rules for `bt full` /
`bt N full` / `info stack`, help text, tab completion).

### `info macro NAME` — first-class single-macro introspection

**What ships**: `info macro <name>` (`i M <name>`) pretty-prints
a macro's params, body, predefined flag, and current call-stack
depth (how many frames are expanding it right now). Handles
"macro not found" cleanly.

```
(pp) i M MAX
MAX(a, b)
  params: a, b
  body  : ((a) > (b) ? (a) : (b))
  on call stack: 1 frame
```

`i M <name>` for an undefined macro prints
`No macro named "<name>" is currently defined.`

**Why existence needs a pre-check**: wave's
`get_macro_definition` does *not* throw for unknown names — it
returns an empty object-like definition. So `show_macro` calls
`ctx.is_defined_macro(name)` first; only if that's true does it
fetch params/body.

**Files**: `src/view.hpp` (`show_macro`, grammar rule
`(lit("macro") | lit("M")) >> +space > anything`, help, tab
completion). No `server.hpp` change — reuses the same
`get_macro_definition` lookup as `print_verbose`/`print_args`.

### Quit crash fix — `strdup` feature-test macro in linenoise.c

**What shipped**: `external/linenoise/linenoise.c` was missing the
POSIX feature-test macro, so under strict c11 `strdup` was
implicitly declared as returning `int` (truncating the pointer),
which corrupted the history list and crashed inside
`linenoiseHistorySave` at exit. Any REPL session that entered the
prompt loop and then exited (via `q` or EOF) segfaulted. Adding
`#define _DEFAULT_SOURCE` before the first `#include` gives
`strdup` and `strcasecmp` proper prototypes.

The `quit()` path was also hardened: it previously threw
`session_terminate` from inside the linenoise callback lambda,
which unwinds across the C `linenoise` call frame (UB). Now
`quit()` sets a `want_quit` flag; the prompt loop breaks cleanly
and re-throws `session_terminate` from a C++-only call stack (no C
frame in between), which `ppstep.cpp`'s main loop catches as
before.

**Files**: `external/linenoise/linenoise.c` (`_DEFAULT_SOURCE`),
`src/view.hpp` (`quit()` sets flag; `prompt()` re-throws after the
loop).

---

## Brainstorm — `info` extensions worth shipping

Each addresses a real "what just happened in the preprocessor?"
question that's currently invisible.

1. ~~**`info stack`** (or extend `bt` to `bt full`)~~ —
   **Landed in Phase 5.** `info stack` / `i s` and `bt full`
   print every expansion frame with bound `param ← arg` tokens
   at each level. `server_state::expanding` is now
   `std::vector<expansion_frame<ContainerT>>` carrying both the
   call tokens and the arguments; previously args lived only on
   the short-lived `events::call` struct.

2. ~~**`info macro NAME`**~~ — **Landed in Phase 5.** `i M NAME`
   pretty-prints params, body, predefined flag, and current
   call-stack depth. Uses `ctx.is_defined_macro` for the
   existence check (wave's `get_macro_definition` returns empty,
   not throws, for unknown names).

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

- ~~`info stack`~~ landed (Phase 5).
- ~~`info macro NAME`~~ landed (Phase 5).
- **`info count`** — partly done already (the bare `info`
  summary prints per-event and per-macro counts); promote to its
  own `info count` command if a dedicated low-cost stat is
  wanted.
- **`info path TOKEN`** as a long-term centerpiece teaching
  feature.
- **`watch TOKEN`**, **`condition N EXPR`**, **`ignore N K`** —
  breakpoint control extensions.
- **Refactor** — split `client.hpp` / `view.hpp` into `.cpp` +
  `.hpp` units (still overdue; both are now larger after Phase 5).

---

## Handoff

Pickup notes for whoever continues work on this codebase after
me. The repo is a Boost.Wave-backed preprocessor debugger with
its own REPL; the recent additions (Phases 1–5 above) live in
five header files plus a vendored linenoise — there is no
project `.cpp`, just headers (`ppstep.cpp` is the only TU).

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
| `src/server_fwd.hpp` | Forward decls for `server`/`server_state`/`expansion_frame`. |
| `src/client.hpp`     | The `client<TokenT, ContainerT>` class, the `events::*` event structs that go in `std::variant<preprocessing_event>`, and `client_cli<TokenT, ContainerT>`. (Note the unusual include direction: `client.hpp` includes `view.hpp`, not the reverse.) |
| `src/server.hpp`     | The Boost.Wave hooks (`expanding_function_like_macro` etc.) and the `expansion_frame<ContainerT>` / `server_state` definitions. |
| `src/view.hpp`       | REPL grammar, `client_cli` impl, linenoise init, completion callback, and the `expanding_trace`/`show_stack`/`show_macro`/`print_frame_args` helpers. Splitting this into a `.cpp` is overdue. |
| `src/utils.hpp`      | Printing helpers and `g_color_enabled`. |
| `external/linenoise/linenoise.c` | Vendored line editor. Needs `_DEFAULT_SOURCE` (added in Phase 5) so `strdup`/`strcasecmp` get prototypes under strict c11. |

### Key invariants established by Phases 1–5

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
   `state->expanding`, do push-first. The same push-before-fire
   discipline now also applies to `state->rescanning` in
   `expanded_macro`: the rescan-frame push happens *before*
   `on_expanded` fires, so the `expanded` prompt sees the body
   already queued (and, via the disabled-set derivation in 10
   below, the macro already painted blue). `rescanned_macro`
   still pops *after* `on_rescanned` fires, so the `rescanned`
   prompt sees the frame still live — symmetric with `expanded`.
4. **`finish` is depth-based, not bp-on-name.**
   `finish_target_depth` is recorded; the next `expanded` event
   at exactly that depth fires. Nested same-name macros
   (e.g. `MAX(MAX(1,2),3)`) work without spuriously hitting
   inner expansions.
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
7. **`state->expanding` is `std::vector<expansion_frame<ContainerT>>`,
   not `std::vector<ContainerT>`.** Phase 5 change: each frame
   carries `call` (the call tokens) and `arguments` (the
   `std::vector<ContainerT>` of bound arg token-lists, empty for
   object-like). Any code iterating the stack must read
   `frame.call...`, not `frame...`. The args live for the frame's
   lifetime, so `info stack` / `bt full` can print `param ← arg`
   at every level — not just the innermost (which is all the
   short-lived `events::call` struct could show before).
8. **`quit()` does not throw across the linenoise C frame.**
   It sets a `want_quit` flag on `client_cli`; the prompt loop
   breaks cleanly and re-throws `session_terminate` from a
   C++-only call stack. Throwing from inside the linenoise
   callback lambda is UB and crashed on exit.
9. **linenoise.c needs `_DEFAULT_SOURCE`.** Without it,
   `strdup` is implicitly declared as returning `int` under
   strict c11, truncating the pointer and crashing
   `linenoiseHistorySave` at exit. If you reintroduce the file
   or compile it elsewhere, keep the feature-test macro.
10. **The disabled ("painted-blue") set is *derived* from
    `state->rescanning`, not tracked separately.** A macro is
    disabled for exactly the lifetime of its rescan frame:
    `expanded_macro` pushes `{call, result}` onto `rescanning`
    (the `call`/`cause` is the macro whose body is being
    rescanned), and `rescanned_macro` pops it. So the disabled
    set = the union of `cause` names across all live
    `rescanning` frames, innermost-first, deduped. This is what
    `info disabled` / `i d` and the `-- disabled (blue) --` pane
    in the frames log print. No separate server state — deriving
    from the same LIFO that mirrors Wave's rescan stack means it
    cannot desync from Wave's own blue-paint set. The push-before-
    fire reorder in invariant 3 is what makes the `expanded`
    prompt see the macro as already blue (the push marks the
    start of the blue window; before the reorder the prompt sat
    before the push and missed it).

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

