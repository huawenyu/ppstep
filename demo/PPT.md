---
title: "C Preprocessor Magic"
sub_title: "Abusing #define for fun and profit"
authors:
  - Huawen Yu
  - Based on jhnet.co.uk/articles/cpp_magic
  - presenterm -x
options:
  command_prefix: "cmd:"
  strict_front_matter_parsing: false
  incremental_lists: false
  implicit_slide_ends: false
  end_slide_shorthand: false
---

<!-- cmd:alignment: center -->
C Preprocessor Magic
===

Abusing `#define` to implement if-statements, recursion, and iterators
in the C preprocessor (CPP).

`presenterm -x cpp_magic.md`

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- `define`
- if-else
- not-bool
- eval
- defer
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## What You'll Learn

The C preprocessor is just text substitution — but with a few tricks
we can make it do things normally impossible without a real language:

* **If-statements** — compile-time branching via `##` concatenation
* **Bool casting** — `!!` operator implemented in macros
* **Recursion** — bypassing the "painted blue" rule
* **Iteration** — `MAP` that applies a macro to every argument

Each trick has a **runnable demo** — press `Ctrl+e` in any code block
to see the preprocessor output live.

All demos use: `gcc -E -P -std=gnu11`

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- `if-else`
- not-bool
- eval
- defer
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## define
===

### The Humble #define

Object-like macros do simple text substitution:

```c +exec
#define VERSION 123

VERSION
```

Function-like macros take arguments. Always parenthesize!

```c +exec
#define MULTIPLY(a, b) ((a) * (b))

MULTIPLY(4 + 2, 2 + 8)
```

Variadic macros use `__VA_ARGS__`:

```c +exec
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)

DEBUG("error %d", 42)
```

This is where sane usage ends. Let's go deeper.

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- `if-else`
- not-bool
- eval
- defer
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## if-else
===

### Pattern Matching with `##`

Goal: `IF_ELSE(condition)(true-branch)(false-branch)`

The trick: use `##` to concatenate `_IF_` with the condition,
producing either `_IF_1` or `_IF_0`:

```bash +exec
cat /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/02_if_else.c
echo "--- OUTPUT ---"
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/02_if_else.c
```

### How It Works

```
IF_ELSE(1)(true)(false)
  -> _IF_ ## 1 (true)(false)
  -> _IF_1 (true)(false)
  -> true _IF_1_ELSE (false)
  -> true               (_IF_1_ELSE swallows the rest)
```

But `IF_ELSE(123)(...)` fails — `_IF_123` is undefined!
We need to cast `123` to `1` first...

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- `not-bool`
- eval
- defer
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## not-bool
===

### NOT and BOOL via Probe Detection

**Step 1:** `SECOND` picks the 2nd argument:

```c
#define SECOND(a, b, ...) b
```

**Step 2:** `PROBE()` secretly expands to two args:

```c
#define PROBE() ~, 1
#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
```

**Step 3:** `NOT` uses pattern matching — only `_NOT_0` is a PROBE:

```c
#define CAT(a,b) a ## b
#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()
```

**Step 4:** `BOOL` = double negation:

```c
#define BOOL(x) NOT(NOT(x))
```

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/03_not_bool.c
```

`CAT` is critical: it hides `##` so args expand before concatenation.

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- `eval`
- defer
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## eval
===

### Forcing Multiple Expansion Passes

Problem: `A EMPTY() (123)` doesn't expand `A` because `EMPTY()`
sits between `A` and `(123)`.

```
A EMPTY() (123)
     ^
     EMPTY() -> (nothing)
A (123)
     ^
     A is NOT followed by () -> not a macro!
```

**Fix:** `EVAL1` forces a second pass by expanding args first:

```c
#define EVAL1(...) __VA_ARGS__
EVAL1(A EMPTY() (123))  // works!
```

**Chain for O(2^n) passes with O(n) macros:**

```c
#define EVAL(...) EVAL1024(__VA_ARGS__)
#define EVAL1024(...) EVAL512(EVAL512(__VA_ARGS__))
#define EVAL512(...)  EVAL256(EVAL256(__VA_ARGS__))
// ... down to EVAL1
```

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/05_eval.c
```

Line 1: no EVAL — `A` not expanded.
Line 2: `EVAL1` — one extra pass, works!
Line 3: `EVAL` — full chain, same result here.

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- eval
- `defer`
- recurse
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## defer
===

### Delaying Macro Expansion

`DEFER1` prevents a macro from expanding in the current pass
by inserting `EMPTY()` between the macro and its arguments:

```c
#define EMPTY()
#define DEFER1(m) m EMPTY()

#define B(n) n is my favourite!

DEFER1(B)(321)       // -> B (321)  [not expanded yet]
EVAL1(DEFER1(B)(321)) // -> 321 is my favourite!
```

### Why This Matters

This is the building block for **recursion**: by deferring a macro,
we prevent it from being "painted blue" (CPP's rule that blocks
self-reference). The expansion happens in a *later* pass where
CPP doesn't see it as recursion.

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/06_defer.c
```

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- eval
- defer
- `recurse`
- map
- has-args
- map-final

<!-- cmd:column: 1 -->

## recurse
===

### Bypassing the Painted-Blue Rule

CPP blocks recursion: if `RECURSE` appears in its own expansion,
it's "painted blue" and never expands again.

**Trick:** `_RECURSE` (note the underscore) expands to `RECURSE`
(without `()`). The `()` comes from the deferred expansion:

```c
#define RECURSE() I am recursive: DEFER1(_RECURSE)()()
#define _RECURSE() RECURSE
```

The two `()` at the end:
- First `()` is consumed by `_RECURSE()` -> produces `RECURSE`
- Second `()` pairs with `RECURSE` -> triggers `RECURSE()`

Each `EVAL` pass unfolds one more level of recursion.

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/07_recurse.c 2>&1 | head -3
```

Line 1: 1 pass — `_RECURSE` deferred.
Line 2: `EVAL1` — one recursion.
Line 3: `EVAL` — many recursions (1024 passes).

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- eval
- defer
- recurse
- `map`
- has-args
- map-final

<!-- cmd:column: 1 -->

## map
===

### Unbounded MAP (No Termination)

`MAP` applies a macro to every argument:

```c
#define MAP(m, first, ...) m(first) DEFER1(_MAP)()(m, __VA_ARGS__)
#define _MAP() MAP

#define GREET(x) Hello, x!

EVAL(MAP(GREET, Mum, Dad, Adam, Joe))
```

**Problem:** Recursion never stops — empty args still recurse,
producing infinite `Hello, !` output.

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/08_map_unbounded.c 2>&1 | head -5
echo "..."
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/08_map_unbounded.c 2>&1 | tail -1
```

The first 4 lines are correct, then garbage.
We need **termination detection**.

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- eval
- defer
- recurse
- map
- `has-args`
- map-final

<!-- cmd:column: 1 -->

## has-args
===

### Detecting End of Argument List

`HAS_ARGS` returns `1` if there are arguments, `0` if empty:

```c
#define FIRST(a, ...) a
#define _END_OF_ARGUMENTS_() 0
#define HAS_ARGS(...) BOOL(FIRST(_END_OF_ARGUMENTS_ __VA_ARGS__)())
```

**The trick:** `_END_OF_ARGUMENTS_` is placed *before* `__VA_ARGS__`.

* No args: `FIRST(_END_OF_ARGUMENTS_)()` -> `_END_OF_ARGUMENTS_()` -> `0`
* With args: `FIRST(_END_OF_ARGUMENTS_ some, args)()` -> `_END_OF_ARGUMENTS_ some()`
  -> non-zero, BOOL casts to `1`

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/09_has_args.c
```

Line 1: `HAS_ARGS()` -> `0` (no args)
Line 2: `HAS_ARGS(some, arguments, here)` -> `1` (has args)

Now we can gate recursion with `IF_ELSE(HAS_ARGS(...))`.

<!-- cmd:end_slide -->

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- define
- if-else
- not-bool
- eval
- defer
- recurse
- map
- has-args
- `map-final`

<!-- cmd:column: 1 -->

## map-final
===

### Working MAP with Termination

Combine all the pieces:

```c
#define MAP(m, first, ...)           \
  m(first)                          \
  IF_ELSE(HAS_ARGS(__VA_ARGS__))(   \
    DEFER2(_MAP)()(m, __VA_ARGS__)  \
  )(                                \
  )
#define _MAP() MAP
```

`DEFER2` (two `EMPTY()`) is needed because `IF_ELSE` adds an
extra expansion pass — `DEFER1` would let `_MAP` expand too
early and get painted blue.

### Demo

```bash +exec
gcc -E -P -std=gnu11 /home/hyu/dotwiki/f-work/me_design/shm_stats/cpp_magic/demos/10_map_final.c
```

**Result:** `Hello, Mum! Hello, Dad! Hello, Adam! Hello, Joe!`

Iteration in the C preprocessor — achieved!

<!-- cmd:end_slide -->

<!-- cmd:jump_to_middle -->

  **THANKS**
  -
---

Source: [jhnet.co.uk/articles/cpp_magic](http://jhnet.co.uk/articles/cpp_magic)

Demos: `cpp_magic/demos/*.c`

<!-- cmd:end_slide -->
