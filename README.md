# ppstep

The C and C++ preprocessors are famous for allowing users to write opaque, hard-to-follow code through macros. These downsides are tolerated because macros also allow code to have a high level of abstraction, which can reduce software complexity and improve developer productivity. Doing the good parts takes dicipline; eliminating the bad parts takes refactoring, and importantly, debugging. PPstep is the debugger that macro writers can use to do that.

<p align="center">
  <i>preprocessing a sequence into a tuple, visualized!</i>
</p>

## Features
- Visually single-step through macro expansion and rescanning until preprocessing is complete
- Set breakpoints on macros for specific preprocessing events, and continue preprocessing between them
- Show backtrace of pending macro expansions, and forward-trace of future macro rescans
- #define/#undef macros mid-preprocessing, and interactively expand macros at any time
- **TODO:** Reverse stepping to rewind preprocssing and view steps from an earlier point
- **TODO:** visualizing #if/#elif/#else branches to explore conditional compilation

## The macro-expansion rule ppstep traces

The C preprocessor expands a token stream following the C standard [§6.10.3] rule, as implemented by Boost.Wave
and visualized by ppstep. ppstep exposes each branch — object-like vs
function-like, raw vs expanded argument, the disabled set — as a distinct
event you can step through, break on, or backtrace.

### Object-like vs function-like macros

A macro with **no parameters** is *object-like* — the name is simply
replaced by its body:

```c
#define PI    3.14159
#define HELLO "hello"

PI        →  3.14159
HELLO     →  "hello"
```

A macro whose name is immediately followed by `(` is *function-like* —
it takes arguments that are bound to parameters before substitution:

```c
#define ADD(a, b) ((a) + (b))

ADD(1, 2)
    ↓
((1) + (2))
```

It is *not* a function — the preprocessor just recognizes the name,
collects the comma-separated arguments, and substitutes.

### The disabled set ("paint-blue") — preventing infinite recursion

While rescanning a macro `M`'s own body, `M` is added to the **disabled
set** so it cannot re-expand itself. This is Dave Prosser's "painting the
macro blue." The disabled set is **per-rescan-scope**, not global: `M`
is only disabled during the rescan of `M`'s body, and is expandable
again elsewhere — including inside *another* macro's body that is being
rescanned.

This is what stops mutual/circular self-reference from looping forever.
Conceptually, expanding `A` disables `A`, sees `B`, expands `B`
(disabling `B`), then sees `A` again — but `A` is already disabled, so it
is left unchanged. The result is just `A`.

The simplest case is two macros that name each other (runnable as
`demo/basic/09_circular.c`):

```c
#define A B
#define B A
A
```

```text
A              expand A → B,   disabled = { A }
  → B          expand B → A,   disabled = { A, B }
  → A          A is disabled → copy literally (no re-expansion)
```

Without the disabled set you might imagine `A → B → A → B → …` forever;
the rule stops the chain the moment a disabled name reappears.

A three-level cycle `A → B → C → A` unwinds the same way: each name is
disabled for its own rescan, and the loop stops the moment the starting
name reappears, also yielding `A`.

> **Disabled does not mean the macro is permanently disabled.** It is
> disabled *while its expansion is being rescanned* — per-rescan-scope,
> not globally.

### Raw vs expanded arguments — what `#` and `##` change

The precise model is **three phases**: argument preparation (a pre-pass),
substitution (form selection per use), and rescan. The common shorthand
"expand the argument first, then substitute" is *wrong* — a single
argument can be used both raw and expanded *in the same body*, which a
single pre-expansion cannot explain.

The case that proves it — the *same* argument `X`, used twice in one
body, prepared two different ways:

```c
#define STR(x)  #x + x
#define X       hello

STR(X)   →   "X" + hello        (NOT "hello" + hello, NOT "X" + X)
```

The `#x` use takes `X` raw → `"X"`; the plain `x` use takes `X` expanded
→ `hello`. One argument, two preparations. The preprocessor computes
*both* forms during a preparation phase, then each use-site picks one:

```text
             F(actual_arg)
                  │
                  ▼
          COLLECT ARGUMENT            ← raw token sequence, nothing expanded
                  │
                  ▼
   PREPARE EACH ARGUMENT (pre-pass, per parameter):
        expand the raw arg → EXPANDED FORM     ← the nested "calling X"
        store BOTH forms:  raw form  AND  expanded form
                  │
                  ▼
          get macro replacement body
                  │
                  ▼
        walk replacement list, per occurrence:
          ├─ param preceded by #  → use RAW form      → stringize
          ├─ param adjacent to ## → use RAW form      → paste
          └─ param otherwise      → use EXPANDED form → splice
                  │
                  ▼
          SUBSTITUTION   (splice the chosen forms into the body)
                  │
                  ▼
            RESCAN    (F painted blue here; result rescanned for more macros)
```

So:

- **argument expansion is a pre-pass**, not part of the body walk. In
  ppstep you see it as a nested `calling X → expanded X` event *between*
  `calling STR` and `expanded STR` — while `STR` is still "current" on
  the call stack and not yet painted blue (the blue window is
  `[expanded STR, rescanned STR]`, the body-rescan phase, which hasn't
  started).
- **`#`/`##` do not suppress the pre-pass** — both forms are always
  computed. They only change **which stored form** a given use-site
  splices in: the raw form, instead of the expanded one. `#` then
  stringizes the raw form; `##` pastes raw tokens.
- **the rescan still runs** after substitution; `#`/`##` turn off the
  argument's expansion at its own use-site, never the body's rescan.

| Operator | which form the use-site splices | what the use produces |
|----------|--------------------------------|----------------------|
| (none)   | EXPANDED form                  | expanded arg spliced; whole body rescanned |
| `#x`     | RAW form                       | string literal of the raw arg text → `"x"` |
| `x##y`   | RAW form                       | the two raw tokens pasted into `xy` |

`#` raws its use-site but the rest of the body still rescans — the `C`
*inside the body* (not the argument) is expanded (runnable as
`demo/basic/10_rescan_existing.c`):

```c
#define C           100
#define STR_PLUS(x) #x + C

STR_PLUS(C)
  ↓                          prepare: arg C → raw "C" + expanded 100
"C" + C                      #x uses raw → "C"; the body's own C stays literal
  ↓                          rescan the substituted body:
"C" + 100                      "C" is a literal (not rescanned internally);
                                 C → 100
```

Contrast the same body *without* `#`. The plain `x` uses the expanded
form, so the argument's `C` becomes `100`; the body's own `C` becomes
`100` on the rescan:

```c
#define F(x)  x + C
#define C     100

F(C)        →  100 + C  →  100 + 100   (arg C → expanded 100; body C → 100 on rescan)
STR_PLUS(C) →  "C" + C  →  "C" + 100   (#x uses raw → "C"; body C → 100 on rescan)
```

`#`/`##` select which stored form a use-site splices; they never turn
off the rescan of the resulting stream.

`##` raws the arguments and the *pasted* token is rescanned — if pasting
produces a macro name, it expands (runnable as `demo/basic/11_rescan_newmacro.c`):

```c
#define FOO        123
#define CAT(a,b)   a ## b
#define MAKE_FOO() CAT(F, OO)

MAKE_FOO()
  ↓                          expand MAKE_FOO → CAT(F, OO)
CAT(F, OO)
  ↓                          a=F, b=OO (RAW — ## involved)
F ## OO   → FOO              paste on the raw token text
  ↓                          rescan FOO:
123                            FOO is a macro → 123
```

But because `##` uses the raw tokens, a macro passed as a `##` argument
is **not** expanded before the paste — it stays literal (runnable as
`demo/basic/05_paste.c`):

```c
#define X        123
#define CAT(a,b) a ## b

CAT(X, Y)
  ↓                          a=X, b=Y (RAW — not expanded)
X ## Y    → XY               (NOT 123Y — X was never expanded)
```

### The macro-expansion concepts flowchart


```text
                    FIND MACRO
                        │
                        ▼
              ┌───────────────────┐
              │ Macro disabled ?  │
              └──────┬───────┬────┘
                   yes│       │no
                      │       │
                      ▼       ▼
                   DON'T    WHAT TYPE?
                   EXPAND      │
                               │
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
              OBJECT-LIKE           FUNCTION-LIKE
                    │                     │
                    │                     ▼
                    │              collect arguments (raw)
                    │                     │
                    │                     ▼
                    │              prepare each arg:
                    │              store RAW form AND EXPANDED form
                    │                     │
                    └──────────┬──────────┘
                               │
                               ▼
                         SUBSTITUTION
                         (per use-site: # / ## splice RAW,
                                       else splice EXPANDED)
                               │
                               ▼
                             RESCAN
                               │
                               ▼
                         FIND MACRO AGAIN
```



### function-like processing

```text

             F(actual_arg)
                  │
                  ▼
          COLLECT ARGUMENT            ← raw token sequence, nothing expanded
                  │
                  ▼
          PREPARE EACH ARGUMENT (pre-pass, per parameter):
            compute RAW form       (the raw tokens)
            compute EXPANDED form  (the fully macro-expanded arg)
                  │
                  ▼
          get macro replacement body
                  │
                  ▼
        walk replacement list, per occurrence:
                  │
          ┌───────┴────────┐
          │                │
      parameter         normal token
          │                │
          ▼                │
   is this use a            │
   # / ## operand?           │
      │                        │
      ┌───┴────┐               │
      │        │               │
     yes       no              │
      │        │               │
      ▼        ▼               │
   splice   splice             │
    RAW     EXPANDED           │
    form     form              │
      │        │               │
      └────┬───┘               │
           ▼                   │
       SUBSTITUTE ◄────────────┘
           │
           ▼
         RESCAN

```


### Inside function-like argument processing

The argument-preparation phase computes **both** forms of each argument
up front, then the per-use fork only *selects* which stored form to
splice in. The earlier flowchart implied the preprocessor re-expands
per use; in fact it expands once and selects:

```text
                argument
                   │
                   ▼
         PREPARE (pre-pass, per parameter):
            compute RAW form       (the raw token sequence)
            compute EXPANDED form  (the fully macro-expanded arg)
                   │
                   ▼
        per USE-SITE in the body:
          is this use preceded by # or adjacent to ## ?
              │             │
             yes            no
              │             │
              ▼             ▼
          splice RAW     splice EXPANDED
           form            form
              │             │
              └──────┬──────┘
                     ▼
                SUBSTITUTE
                     │
                     ▼
                   RESCAN
```

> **`#` / `##` select the *raw* form at their use-site, instead of the
> *expanded* form.** They do not suppress the pre-pass that computes the
> expanded form, and they do not turn off the rescan of the substituted
> body. `#` stringizes the raw form; `##` pastes raw tokens; an
> ordinary use splices the expanded form. The rescan always runs.

### Expand vs rescan — two distinct phases

1. **Expand** — look up the macro name, substitute its body (with
   arguments bound to parameters). This is the `calling` → `expanded`
   pair in ppstep.
2. **Rescan** — re-examine the substituted result for further macro
   names. This is the `rescanned` event. One source-level call can
   trigger a chain of expand → rescan → expand → rescan → … cycles
   until no macro names remain.

### The data structures ppstep tracks

| Structure | ppstep field | Pushed on | Popped on | Answers |
|-----------|-------------|-----------|-----------|---------|
| **Call stack** | `expanding` | `calling` | `expanded` | Which macro is being substituted RIGHT NOW? |
| **Rescan queue** | `rescanning` | `expanded` | `rescanned` | What token sequences are waiting to be re-scanned? |
| **Disabled set** | *(derived from `rescanning`)* | `expanded` | `rescanned` | Which macros are painted blue (cannot re-expand) right now? |

They grow and shrink in alternation: `calling` pushes the call stack,
`expanded` pops it and pushes the rescan queue, `rescanned` pops the
rescan queue. When both are empty, expansion is complete.

The **disabled set** is not a separate structure — it is the union of the
*cause* names across all live `rescanning` frames (each frame's cause is
the macro whose body is being rescanned, which is precisely the macro
painted blue for that frame's lifetime). So a macro is blue during
`[expanded, rescanned]` of its own body. Inspect it with `info disabled`
/ `i d`, or watch it live in the `-- disabled (blue) --` pane of the
frames log.

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

### A full worked example — all three concepts at once

Take a case that exercises function-like macros, argument expansion, and
the disabled set across nested calls:

```c
#define C       100
#define B(x)    x + 1
#define A(x)    B(x)
#define ADD(a,b) ((a)+(b))

ADD(A(C), B(2))
```

When expanding `ADD`, each argument is itself a macro call (`A(C)`
and `B(2)`), so argument expansion runs them first — `A(C) → B(C) →
C + 1 → 100 + 1` and `B(2) → 2 + 1` — before substituting into
`((a)+(b))` to get `((100 + 1) + (2 + 1))`, which is then rescanned
but contains no further macros. Each call gets its own disabled-set
entry for the duration of its rescan, and the nested argument
expansions happen *before* `ADD`'s body is substituted — because `a`
and `b` are ordinary (non-`#`, non-`##`) parameters.

### Three independent concepts — keep them separate

1. **Object-like vs function-like** → *what kind of macro is this?*
2. **Raw vs expanded argument** → *which stored form does each use-site
   splice in?* (the expanded form is computed up front; `#`/`##` select
   the raw form at their own use-site)
3. **Disabled macro** → *can this macro be expanded at this point in the
   recursive rescan?* (the "paint-blue" disabled set, live in `info
   disabled` / the frames log)

### Worked examples — the `demo/` directory

The `demo/` directory contains one small `.c` file per branch of the
rule, each annotated with the expected trace and result. The core sequence
lives under `demo/basic/`; `demo/cpp_magic/` holds extended examples:

| File | Branch | What it shows |
|------|--------|---------------|
| `basic/01_ordinary.c` | ordinary token | Non-macro tokens copied verbatim — no call/expand/rescan |
| `basic/02_selfref.c` | object macro | Self-reference: `#define foo foo+1` — `foo` painted blue during its own rescan |
| `basic/03_funcargs.c` | function macro | Normal arg expansion: `ID(M)` → arg `M` expanded to `42` before substitution |
| `basic/04_1_stringify.c` | function macro + `#` | `#` selects the raw form at its use-site: `STR(X)` body `#x + x` → `"X" + hello` (one arg, two forms) |
| `basic/04_2_stringify.c` | function macro + `#` | stringify the *value*: `XSTR(X)`→`"123"` — ordinary param expands first, then `#` stringizes the expanded form |
| `basic/05_paste.c` | function macro + `##` | `##` selects the raw form: `CAT(NAME,_suffix)` → `NAME_suffix` (not `var_suffix`) |
| `basic/06_nested_rescan.c` | function macro rescan | Rescan finds a macro in the substituted *body* (`TAIL`), not the argument: `A(42)` → `42 + 99` |
| `basic/07_deep_nesting.c` | full pipeline | 3-level nesting: `C → B(42) → A(42+1) → [42+1]` — call stack + rescan queue |
| `basic/08_paintblue.c` | disabled-set scope | Paint-blue is per-rescan-scope: `X` is expandable through `WRAP(X)` and as a standalone `X` |
| `basic/09_circular.c` | object macro cycle | Mutual/circular self-reference (`A↔B`, `P→Q→R→P`): disabled set stops the infinite loop |
| `basic/10_rescan_existing.c` | "existing tokens that can expand" | `#` raws the arg (`"C"`); the body's `C` is an existing token the rescan expands → `"C" + 100` |
| `basic/11_rescan_newmacro.c` | "newly formed macro name" | `##` paste synthesizes a macro name the rescan expands: `MAKE_FOO()` → `FOO` → `123` |
| `basic/13_all_cases.c` | all branches | One invocation per branch of the expansion rule — every case in a single session |
| `basic/14_rescan_flow.c` | the `##`/rescan flow | Walks the "complete `##`/rescan flow" draw — RAW ARG / EXPAND ARG / newly-formed / existing — one case per branch |

Run any of them with, e.g.:

```sh
ppstep demo/basic/03_funcargs.c
```

The frames log at `/tmp/ppstep.log` mirrors the live call stack,
rescan queue, and disabled (blue) set — `tail -F` it in another terminal
while stepping.

## Building

```sh
 ### Install dependencies
sudo apt install build-essential 
sudo apt install libboost-all-dev
sudo apt install cmake

 ### build from source
git clone https://github.com/huawenyu/ppstep.git
cd ppstep
mkdir build && cd build && cmake .. && make

OR: change cmake .. to:
   cmake -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ ..
```

## Usage

To try it out, run `ppstep your-source-file.c`. `ppstep` supports common preprocessor flags like:
```sh
  --include/-I   add include directories,
  --define/-D    define macros,
  --undefine/-U  undefine macros,

Usage: rlwrap [-options] -z ./filter.py <command>

Source:
    https://github.com/notfoundry/ppstep
    /nix/store/238vrcj4b0fcp0yivxcp7mzl1m9jrmkk-rlwrap-0.46.1/share/rlwrap/filters/rlwrapfilter.py

Pre-requirement:
    clang-format: nix-env -iA nixpkgs.clang-tools

pp-shell:
prompt: pp>
commands:
    q-quit
    s-step
    c-continue

    bt-backtrace
    ft-forwardtrace
    b-break: break call <macro>, break expand <macro>
    d-delete: delete call <macro>
    i-info: info stack, info args, info disabled, info macro <name>
process-status:
    called,exanded,rescanned,lexed

```

#### The Prompt

You should see a prompt that looks like `pp>`. 
- From here, you can step forward through preprocessing steps using the `step` or `s` commands, and see visually what each step does.  
- You will notice that the prompt will have a suffix added to it to show what the current preprocessing step is,  
  such as `called`, `expanded`, `rescanned`, or `lexed`.  
- Newly-encountered macro calls, finished macro expansions, and finished macro rescans are each color-coded in the visual output so you can see where changes were made.  
- When you are done, you can use the `quit` or `q` commands to exit the prompt.

- While stepping, if you want to see the history of pending macro expansions, you can use the `backtrace` or `bt` commands.  
- You can also look into the future to see what the anticipated macro rescans will be by using the `forwardtrace` or `ft` commands.
- To see which macros are currently "painted blue" (disabled, so they cannot re-expand during the current rescan — the rule that stops `A → B → A` from looping forever), use `info disabled` or `i d`. The same set is mirrored in the `-- disabled (blue) --` pane of the frames log.

#### Breakpoints
- If there is a specific macro and preprocessing step that you are interested in visualizing, you can set a breakpoint on that macro using the `break` or `b` commands.
- To break when a specific macro is called, for example, you could enter `break call YOUR_MACRO` or `bc YOUR MACRO`.
- Similarly to break when that macro is finished expanding, you could enter `break expand YOUR_MACRO` or `be YOUR_MACRO`.
- To continue preprocessing until one of these breakpoints is hit (or preprocessing is finished), use the `continue` or `c` commands.  
- Deleting a breakpoint has a similar syntax to setting them: the complements to `break call YOUR_MACRO` or `bc YOUR_MACRO` are `delete call YOUR_MACRO` or `dc YOUR_MACRO`.

#### Interactive Evaluation

If you choose to, you can also use preprocessor directives mid-preprocessing.  
For example, you could say `#define NEW_MACRO(x) x` to create a function-like macro named `NEW_MACRO` in real-time. `#include` and `#undef` also work as expected (though undefining a macro in the process of being expanded without then re-defining another macro under that name can have terrible consequences!) Macros can also be expanded mid-preprocessing with the `expand` or `e` commands.
For example, `expand NEW_MACRO(1)` would open a nested prompt allowing you to step through each of the expansion stages of `NEW_MACRO`.
