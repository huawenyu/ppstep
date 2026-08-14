// Pitfall — `##` has the same trap (paste uses the RAW argument)
//
//   CAT(X, Y)  →  XY       (NOT foobar)
//   XCAT(X, Y) →  foobar
//
// Parameters adjacent to `##` use the *raw* argument — argument expansion is
// suppressed for an operand of `##`. So `X` and `Y` are NOT expanded to
// `foo`/`bar`; the raw text `X ## Y` pastes to `XY`.
//
// The usual fix is another macro level: `XCAT` has an ORDINARY param, so its
// args expand first (`X`→`foo`, `Y`→`bar`), then the body `CAT(a,b)` pastes the
// already-expanded values → `foobar`.
//
// Run:  ppstep demo/basic/17_pitfall_paste.c
// Try:  b c CAT ; c ; s s   (raw args pasted, never expanded)
//       b c XCAT ; c ; s s  (args expand first, then paste)
//
// Trace (CAT):
//   1. calling  CAT         — args: a=X, b=Y (raw)
//      ── ## operand → only RAW form used ──
//   2. expanded CAT         — a ## b → X ## Y → XY
//   3. rescanned CAT         — XY: no macros → copy
//   Result:  XY
//
// Trace (XCAT):
//   1. calling  XCAT        — args: a=X, b=Y (raw)
//      ── argument preparation pre-pass (ordinary params) ──
//   2. expanded XCAT        — body CAT(a,b) splices EXPANDED → CAT(foo, bar)
//   3. rescanned XCAT       — CAT(foo, bar): FUNCTION-LIKE
//   4. calling  CAT          — args: a=foo, b=bar (raw)
//   5. expanded CAT         — a ## b → foo ## bar → foobar
//   Result:  foobar

#define CAT(x, y)   x##y
#define XCAT(x, y)  CAT(x, y)
#define X  foo
#define Y  bar

CAT(X, Y)
XCAT(X, Y)
