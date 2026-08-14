// Pitfall — the two-level macro trick (force expansion before `#`)
//
//   XSTR(FOO)  →  "100"
//
// `XSTR` has an ORDINARY param `x` (no `#`), so the argument IS expanded
// before substitution: `FOO` → `100`. The body `STR(x)` then becomes
// `STR(100)`, and the rescanned `STR(100)` stringizes the already-expanded
// `100` → `"100"`. This is how you stringify the VALUE of a macro, not its
// name.
//
// Run:  ppstep demo/basic/16_pitfall_twolevel.c
// Try:  b c XSTR ; c ; s s   (see the arg-expansion pre-pass before STR)
//
// Trace:
//   1. calling  XSTR       — collect arg: x = raw "FOO"
//      ── argument preparation pre-pass runs here (XSTR not yet blue) ──
//   2. calling  FOO         — expand the raw arg FOO → "100" (EXPANDED form)
//   3. expanded XSTR       — body STR(x) splices EXPANDED → STR(100)
//   4. rescanned XSTR      — STR(100): FUNCTION-LIKE
//      ── now STR runs on the already-expanded value ──
//   5. calling  STR         — arg 100 (raw)
//   6. expanded STR         — #x splices raw → "100"
//   7. rescanned STR        — "100": no macros → copy
//
// Result:  "100"

#define STR(x)   #x
#define XSTR(x)  STR(x)
#define FOO      100

XSTR(FOO)
