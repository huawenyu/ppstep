# ppstep demos — understanding the C macro-expansion rule

The C preprocessor expands tokens following this rule (from the C standard
[§6.10.3], as implemented by Boost.Wave and traced by ppstep):

```
expand(tokens, disabled)
    │
    ├── ordinary token
    │      └── copy to output as-is
    │
    ├── object macro M
    │      ├── substitute replacement (with M added to disabled)
    │      └── rescan(replacement, disabled ∪ {M})
    │
    └── function macro F(args)
           ├── collect arguments (raw)
           ├── PREPARE each arg: store RAW form AND EXPANDED form
           │     (one pre-pass per parameter; both forms computed once)
           ├── walk body, per use-site:
           │     ├── # / ## operand  → splice RAW form
           │     └── otherwise       → splice EXPANDED form
           ├── substitute (with F added to disabled)
           └── rescan(result, disabled ∪ {F})
```

> **Three phases, not "expand-then-substitute".** Argument preparation is a
> *separate pre-pass* that computes BOTH the raw and the expanded form of each
> parameter, once. `#` and `##` do **not** suppress that phase — they only make
> their own use-site splice the stored *raw* form instead of the stored
> *expanded* one. Other uses of the same parameter still splice the expanded
> form. See `04_1_stringify.c` (`#x + x` → `"X" + hello`: one argument, two
> forms, selected per use-site); `04_2_stringify.c` stringifies the *value*
> (`XSTR(X)` → `"123"`, expanding the ordinary param first).

## Key concepts

### The disabled set ("paint-blue")

When a macro `M` is expanded, `M` is added to the **disabled set** before
rescanning its replacement. This prevents `M` from re-expanding itself
within its own body — a rule often called "painting the macro blue" (after
Dave Prosser's classic RFU notation). The disabled set is **per-rescan-scope**:
`M` is only disabled while rescanning `M`'s own replacement, not globally.
If `M` appears again later — or inside *another* macro's body that is
being rescanned — it IS expandable there. See `08_paintblue.c`.

### Raw vs expanded arguments — which form does each use-site get?

For a **function-like** macro, a single argument-preparation pre-pass computes
**two forms** of each parameter up front: the **RAW** form (the argument text
exactly as written) and the **EXPANDED** form (that text run through full macro
expansion). The substitution walk then **selects** which stored form each
use-site splices in:

| Use of the parameter | Form spliced | What it does |
|----------------------|--------------|-------------|
| `#x` (stringify)     | RAW          | Stringize the raw text → `"x"`, then the literal is rescanned |
| `x ## y` (paste)     | RAW          | Paste raw tokens → `xy`, then the result is rescanned |
| ordinary `x`         | EXPANDED     | Splice the already-expanded arg, then the result is rescanned |

`#` and `##` do **not** run a different argument-expansion phase — they read a
different *stored* form. That's why `#x + x` can take both forms from one
argument in a single body (`"X" + hello`). The pasted/stringified/spliced
result is **always** rescanned afterward; only the argument *form* differs,
never whether a rescan happens.

### Expand vs. rescan — two distinct phases

1. **Expand** — look up the macro name, substitute its body (with arguments
   bound). This is the `calling` → `expanded` pair in ppstep.
2. **Rescan** — re-examine the substituted result for further macro names.
   This is the `rescanned` event. A single source-level macro call can
   trigger a chain of expand → rescan → expand → rescan → ... cycles
   until no macro names remain.

## Two data structures ppstep tracks

| Structure | ppstep field | Pushed on | Popped on | Answers |
|-----------|-------------|-----------|-----------|---------|
| **Call stack** | `expanding` | `calling` | `expanded` | Which macro is being substituted RIGHT NOW? |
| **Rescan queue** | `rescanning` | `expanded` | `rescanned` | What token sequences are waiting to be re-scanned? |

- The call stack is a stack of macro *calls* — pushed when a macro is
  invoked, popped when its body has been substituted.
- The rescan queue is a stack of *token sequences* awaiting rescan —
  pushed when a body is produced, popped when that sequence has been
  fully re-scanned.
- They grow/shrink in alternation: `calling` pushes the call stack,
  `expanded` pops it and pushes the rescan queue, `rescanned` pops the
  rescan queue. When both are empty, expansion is complete.

```
                    call stack      rescan queue
calling C           [C]            []
expanded C          []             [B(42)]
calling B           [B]            [B(42)]
expanded B          []             [A(42+1)]
calling A           [A]            [A(42+1)]
expanded A          []             [[42+1]]
rescanned A         []             [B(42)]      ← A's entry consumed
rescanned B         []             [C]          ← B's entry consumed
rescanned C         []             []           ← done
```

## Demos

| File | Branch | What it shows |
|------|--------|---------------|
| `basic/01_ordinary.c` | ordinary token | Non-macro tokens are just copied — no call/expand/rescan |
| `basic/02_selfref.c` | object macro | Self-reference: `#define foo foo+1` — `foo` painted blue during its own rescan |
| `basic/03_funcargs.c` | function macro | Normal arg expansion: `ID(M)` → arg `M` expanded to `42` before substitution |
| `basic/04_1_stringify.c` | function macro + `#` | `#` selects the RAW form at its use-site: `STR(X)` body `#x + x` → `"X" + hello` (one arg, two forms) |
| `basic/04_2_stringify.c` | function macro + `#` | stringify the value: `XSTR(X)`→`"123"` (ordinary param expands first, then `#` stringizes) |
| `basic/05_paste.c` | function macro + `##` | `##` selects the RAW form at its use-site: `CAT(NAME,_suffix)` → `NAME_suffix` (not `var_suffix`) |
| `basic/06_nested_rescan.c` | function macro rescan | Rescan finds a macro in the substituted *body* (`TAIL`), not the argument: `A(42)` → `42 + 99` |
| `basic/07_deep_nesting.c` | full pipeline | 3-level nesting: `C → B(42) → A(42+1) → [42+1]` — call stack + rescan queue growing/shrinking |
| `basic/08_paintblue.c` | disabled-set scope | Paint-blue is per-rescan-scope: `X` is expandable through `WRAP(X)` and as a standalone `X` |
| `basic/09_circular.c` | object macro cycle | Mutual/circular self-reference (`A↔B`, `P→Q→R→P`): disabled set stops the infinite loop |
| `basic/10_rescan_existing.c` | "existing tokens that can expand" | `#` raws the arg (`"C"`); the body's `C` is an existing token the rescan expands → `"C" + 100` |
| `basic/11_rescan_newmacro.c` | "newly formed macro name" | `##` paste synthesizes a macro name the rescan expands: `MAKE_FOO()` → `FOO` → `123` |
| `basic/13_all_cases.c` | all branches | One invocation per branch of the expansion rule — every case in a single session |
| `basic/14_rescan_flow.c` | the `##`/rescan flow | Walks the "complete `##`/rescan flow" draw — RAW ARG / EXPAND ARG / newly-formed / existing — one case per branch |

## Running a demo

```sh
ppstep demo/basic/03_funcargs.c
```

The frames log at `/tmp/ppstep.log` mirrors the live call stack
and rescan queue — `tail -F` it in another terminal while stepping.

### Commands

| Command | Action |
|---------|--------|
| `s` / `step` | Step to next macro event (skips non-macro tokens) |
| `s N` / `step N` | Step exactly N events (includes individual tokens) |
| `n` / `next` | Same as bare `step` |
| `c` / `continue` | Run until breakpoint or end |
| `b c <macro>` | Break on macro call |
| `b e <macro>` | Break on macro expand |
| `b r <macro>` | Break on macro rescan |
| `b l <token>` | Break on a specific lexed token |
| `bt` / `backtrace` | Show call stack (expansion frames) |
| `ft` / `forwardtrace` | Show rescan queue |
| `i s` / `info stack` | Show all expansion frames with bound args |
| `i a` / `info args` | Show arg → param bindings at current call |
| `i d` / `info disabled` | Show macros painted blue (per-rescan-scope disabled set) |
| `i b` / `info breakpoints` | List breakpoints |
| `d <macro>` | Delete a call breakpoint by name |
| `q` / `quit` | Exit |
