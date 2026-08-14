---
title: "The C and C++ preprocessors"
authors:
  - Huawen Yu
  - presenterm -x [^1]
  - 2026-08-12
options:
  command_prefix: "cmd:"
  strict_front_matter_parsing: false
  incremental_lists: false
  implicit_slide_ends: false
  end_slide_shorthand: false
---

<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- `1. pitfall: string 1/2`
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 2 (menu 1): pitfall: string -->

**classic `#` pitfall**

```c
#define STR(x)  #x
#define FOO     100
STR(FOO)  ->  "FOO"
```

```text
FOO
 │  x used by #
 ↓
RAW ARGUMENT (don't expand)
 ↓
#x  ->  "FOO"
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/15_pitfall_stringify.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- `1. pitfall: string 2/2`
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 3 (menu 1): pitfall: string (case 2) -->

**"two-level macro" trick**

```c
#define STR(x)   #x
#define XSTR(x)  STR(x)
#define FOO      100
XSTR(FOO)  ->  "100"
```

```text
XSTR(FOO)
  ordinary param -> expand arg: FOO -> 100
  body STR(x) -> STR(100)
  RESCAN: #x -> RAW "100" -> "100"
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/16_pitfall_twolevel.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- `2. pitfall: cat '##'`
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 4 (menu 2): pitfall: cat '##' -->

`##` has the **same trap** as `#`: operands of `##` use the raw argument. `CAT(X,Y)` -> `XY` (not `foobar`); add a level (`XCAT`) to expand first.

```c
#define CAT(x, y)   x##y
#define XCAT(x, y)  CAT(x, y)
#define X  foo
#define Y  bar
CAT(X, Y)   ->  XY
XCAT(X, Y)  ->  foobar
```

```text
CAT(X, Y):  a=X, b=Y (raw)  ->  X ## Y  ->  XY
XCAT(X,Y): ordinary param -> expand: X->foo, Y->bar
           -> CAT(foo, bar) -> foo ## bar -> foobar
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/17_pitfall_paste.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- `3. draw: flow`
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 5 (menu 3): draw: flow -->

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
                   PLAIN       WHAT TYPE?
                               │
                               │
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
              OBJECT-LIKE           FUNCTION-LIKE
                    │                     │
                    │                     ▼
                    │              collect args
                    │                     │
                    │                     ▼
                    │       args (#/## -> RAW; else -> EXPAND)
                    │                     │
                    └──────────┬──────────┘
                               │
                               ▼
                         SUBSTITUTION
                               │
                               ▼
                             RESCAN
                               │
                               ▼
                         FIND MACRO AGAIN
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- `4. draw: function`
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 6 (menu 4): draw: function -->

```text
             F(arg)
                │
                ▼
          COLLECT ARGS
            (raw)
                │
                ▼
          PREPARE ARGS
          RAW + EXPANDED
                │
                ▼
            get body
                │
                ▼
         walk body, per use:
                │
      ┌─────────┴─────────┐
      ▼                   ▼
   param                plain token
      │                   │
  # / ## ?                │
  │       │               │
 yes      no              │
  │       │               │
  ▼       ▼               │
 RAW     EXPANDED         │
  │       │               │
  └───┬───┘               │
      ▼                   │
  SUBSTITUTE ◄────────────┘
      │
      ▼
    RESCAN
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- `5. object-like`
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 7 (menu 5): object-like -->

## Type:
- **No params** → *object-like* (name → body).
- ** have `()` follows name** → *function-like* (binds args to params).

```c
#define PI    3.14159      // object-like
#define ADD(a,b) ((a)+(b)) // function-like
ADD(1,2)  ->  ((1)+(2))
```

### 5. object-like

```c
#define GREETING hello
GREETING X world
```


```bash +exec +acquire_terminal
ppstep ./demo/basic/01_ordinary.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- `6. function-like`
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 8 (menu 6): function-like -->

### 6. function-like

```c
#define ID(x)    x
#define M        42
ID(M)
```

```text
FIND MACRO -> ID
  FUNCTION-LIKE -> collect args: M (raw)
  PREPARE: expand M -> 42 (EXPANDED form)
  SUBSTITUTION: splice 42
  -> 42
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/03_funcargs.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- `7. draw: disabled`
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 9 (menu 7): draw: disabled -->

```text
                    FIND MACRO
                        │
                        ▼
              ┌───────────────────┐
 self-ref ──> │ Macro disabled ?  │
 & loop       └──────┬───────┬────┘
                   yes│       │no
                      │       │
                      ▼       ▼
                   PLAIN       WHAT TYPE?
                               │
                               │
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
              OBJECT-LIKE           FUNCTION-LIKE
                    │                     │
                    │                     ▼
                    │              collect args
                    │                     │
                    │                     ▼
                    │       args (#/## -> RAW; else -> EXPAND)
                    │                     │
                    └──────────┬──────────┘
                               │
                               ▼
                         SUBSTITUTION
                               │
                               ▼
                             RESCAN
                               │
                               ▼
                         FIND MACRO AGAIN
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- `8. self-ref`
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 10 (menu 8): self-ref -->

### 8. self-ref

```c
#define foo foo + 1
foo
```

```text
FIND MACRO -> foo
  Macro disabled? no -> OBJECT-LIKE
  -> SUBSTITUTION (foo + 1)
  -> RESCAN: foo is disabled -> copy literally
result: foo + 1
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/02_selfref.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- `9. loop`
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 11 (menu 9): loop -->

### 9. loop

```c
#define A B
#define B A
A
```

```text
A              expand A -> B,   disabled = { A }
  -> B         expand B -> A,   disabled = { A, B }
  -> A         A is disabled -> copy literally (no re-expansion)
result: A
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/09_circular.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- `10. draw: args`
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 12 (menu 10): draw: args -->

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
                   PLAIN       WHAT TYPE?
                               │
                               │
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
              OBJECT-LIKE           FUNCTION-LIKE
                    │                     │
                    │                     ▼
                    │              collect args
                    │                     │
                    │                     ▼
          paste ──> │       args (#/## -> RAW; else -> EXPAND)
        stringify   │                     │
                    └──────────┬──────────┘
                               │
                               ▼
                         SUBSTITUTION
                               │
                               ▼
                             RESCAN
                               │
                               ▼
                         FIND MACRO AGAIN
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- `11. prepare-args 1/3`
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 13 (menu 11): prepare-args -->

**demo 1 -- two args (error)**

```c
#define F(x)    x
F(a, b)
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/18_1_prepare_2args.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- `11. prepare-args 2/3`
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 14 (menu 11): prepare-args (case 2) -->

**demo 2 -- resolve with parentheses**

```c
#define F(x)    x
F((a, b))  ->  ( a, b )     // 1 arg -- comma inside parens
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/18_2_prepare_parens.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- `11. prepare-args 3/3`
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 15 (menu 11): prepare-args (case 3) -->

**demo 3 -- comma from expansion doesn't split args**

```c
#define F(x)    x
#define PAIR    a, b
F(PAIR)   ->  a, b          // 1 arg PAIR, then expanded
```


```bash +exec +acquire_terminal
ppstep ./demo/basic/18_3_prepare_pair.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- `12. stringify 1/2`
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 16 (menu 12): stringify -->

**case 1** -- one arg, two preparations: `#x` uses *raw*, plain `x` uses *expanded*:

```c
#define STR(x)  #x + x
#define X       hello
STR(X)
```

| use-site | splices | produces |
|----------|---------|----------|
| `#x`     | RAW | `"X"` |
| `x`      | EXPANDED | `hello` |

```bash +exec +acquire_terminal
ppstep ./demo/basic/04_1_stringify.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- `12. stringify 2/2`
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 17 (menu 12): stringify (case 2) -->

**case 2** -- stringify the *value* (XSTR idiom): ordinary param expands first, then `#` stringizes:

```c
#define STR(x)  #x
#define XSTR(x) STR(x)
#define X 123
XSTR(X)  ->  "123"
```

```text
XSTR(X)
  arg X -> expand -> 123 (EXPANDED)
  body: STR(x) -> splice EXPANDED -> STR(123)
  RESCAN: #x -> RAW "123" -> "123"
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/04_2_stringify.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- `13. paste 1/2`
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 18 (menu 13): paste -->

### 12. paste

```c
#define CAT(x,y)  x##y
#define NAME      wad_http_
CAT(NAME, h2)
```

```text
CAT(NAME, h2)
  FUNCTION-LIKE -> args: NAME, h2 (raw)
  PREPARE: expand NAME -> wad_http_ (EXPANDED)
  ## -> RAW form: NAME ## h2 -> NAMEh2
  (NOT wad_http_h2 -- NAME not expanded for ##)
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/05_paste.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- `13. paste 2/2`
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 19 (menu 13): paste (case 2) -->

**fix: expand the argument first** -- add a level (`CAT`) with an ordinary param, so `NAME` -> `wad_http_` *before* `##` sees it (the raw `##` paste moves to `CAT_`):

```c
#define CAT_(x,y)  x##y
#define CAT(x,y)   CAT_(x,y)
#define NAME       wad_http_
CAT(NAME, h2)  ->  wad_http_h2
```

```text
CAT(NAME, h2)
  ordinary param -> expand NAME -> wad_http_ (EXPANDED)
  body CAT_(x,y) -> CAT_(wad_http_, h2)
  RESCAN: wad_http_ ## h2 -> wad_http_h2
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/06_paste_fix.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- `14. draw: rescan`
- 15. rescan-nested
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 20 (menu 14): draw: rescan -->

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
                   PLAIN       WHAT TYPE?
                               │
                               │
                    ┌──────────┴──────────┐
                    │                     │
                    ▼                     ▼
              OBJECT-LIKE           FUNCTION-LIKE
                    │                     │
                    │                     ▼
                    │              collect args
                    │                     │
                    │                     ▼
                    │       args (#/## -> RAW; else -> EXPAND)
                    │                     │
                    └──────────┬──────────┘
                               │
                               ▼
                         SUBSTITUTION
                               │
                    nested ──> ▼
                       ┌───────────────┐
                       │    RESCAN     │ <── loop (disabled)
                       └──────┬────────┘
                               │
                               ▼
                         FIND MACRO AGAIN
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- `15. rescan-nested`
- 16. rescan-exist
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 21 (menu 15): rescan-nested -->

### 13. rescan-nested

```c
#define A(x)  [x]
#define B(y)  A(y+1)
#define C     B(42)
C
```

```text
C -> expand -> B(42) -> rescan
  B(42) -> expand -> A(42+1) -> rescan
    A(42+1) -> expand -> [42+1] -> rescan
      [42+1] -> no macros -> done
result: [ 42 + 1 ]
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/07_deep_nesting.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- `16. rescan-exist`
- 17. rescan-new
<!-- cmd:column: 1 -->
<!-- page 22 (menu 16): rescan-exist -->

### 14. rescan-exist

```c
#define C           100
#define STR_PLUS(x) #x + C
STR_PLUS(C)
```

```text
STR_PLUS(C)
  arg C -> raw "C" + expanded 100
  #x -> RAW -> "C"; body's own C stays literal
  RESCAN: "C" is literal; C -> 100
result: "C" + 100
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/10_rescan_existing.c
```

<!-- cmd:end_slide -->
<!-- cmd:column_layout: [1, 3] -->
<!-- cmd:column: 0 -->

**Menu**
- 1. pitfall: string
- 2. pitfall: cat '##'
- 3. draw: flow
- 4. draw: function
- 5. object-like
- 6. function-like
- 7. draw: disabled
- 8. self-ref
- 9. loop
- 10. draw: args
- 11. prepare-args
- 12. stringify
- 13. paste
- 14. draw: rescan
- 15. rescan-nested
- 16. rescan-exist
- `17. rescan-new`
<!-- cmd:column: 1 -->
<!-- page 23 (menu 17): rescan-new -->

### 15. rescan-new

```c
#define F          bar
#define FOO        123
#define CAT(a,b)   a ## b, a
#define MAKE_FOO() CAT(F, OO)
MAKE_FOO()
```

```text
MAKE_FOO()
  -> CAT(F, OO)
  body `a ## b, a` -- same arg a, two use-sites:
    `a ## b` -> RAW a ## RAW b -> F ## OO -> FOO  (paste)
    `, a`    -> EXPANDED a -> bar
  -> FOO, bar
  RESCAN: FOO is a macro -> 123 ; bar not a macro
result: 123, bar
```

```bash +exec +acquire_terminal
ppstep ./demo/basic/11_rescan_newmacro.c
```

<!-- cmd:end_slide -->
<!-- cmd:jump_to_middle -->
<!-- page 24 (menu 17): thanks -->
  **THANKS**
  -
---
<!-- cmd:end_slide -->

[^1]: [presenterm](https://github.com/mfontanini/presenterm)
