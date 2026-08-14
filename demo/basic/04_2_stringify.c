// Branch 3b — # selects the RAW form at its use-site (stringify)
//
//   └── function macro F(args) → collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  → splice RAW form
//                                   otherwise       → splice EXPANDED form
//                                rescan(result, disabled ∪ {F})
//
// The argument-preparation phase computes BOTH forms up front. `#` does
// NOT suppress that phase — it only makes its own use-site splice the RAW
// form (then stringize it) instead of the EXPANDED one. Other uses of the
// same parameter still splice the EXPANDED form. The body here is `#x + x`,
// so the SAME argument X is used two ways in one body:
//
//   #x  → raw form "X" → stringize → "X"
//    x  → expanded form (X → hello) → hello
//
// Run:  ppstep demo/basic/04_2_stringify.c
// Try:  b c STR    (break on call STR)
//       c          (continue to the STR call)
//       s s s s    (step — note the nested `calling X` between
//                   `calling STR` and `expanded STR`: that IS the
//                   argument-expansion pre-pass; STR is NOT yet blue)
//       i d        (show the disabled set — empty during arg expansion,
//                   STR only blue after `expanded STR`)
//
// Trace:
//   1. calling  STR        — collect arg: x = raw "X"
//      ── argument preparation pre-pass runs here (STR not yet blue) ──
//   2. calling  X           — expand the raw arg X → "hello" (EXPANDED form)
//   3. expanded X           — arg prepared: raw="X", expanded="hello"
//      ── now substitute the body, splicing per use-site ──
//   4. expanded STR        — #x splices raw → "X";  x splices expanded → "hello"
//                            result: "X" + hello
//   5. rescanned STR       — "X" + hello: no macros → copy
//
// Result:  "X" + hello
// The `#x` use took the raw form ("X"); the plain `x` use took the
// expanded form (hello). One argument, two forms, selected per use.


// Contrast: the classic XSTR idiom. `XSTR` takes an ORDINARY param `x`
// (no `#`), so the argument IS expanded before substitution: `X` → `123`.
// Then `STR(123)` stringizes the (already-expanded) value → "123".
// This is how you stringify the VALUE of a macro, not its name.
//
// `#undef X` first: redefining a macro to a DIFFERENT body is illegal
// (C §6.10.3p2), and Wave keeps the old definition with a warning,
// which would make this case produce "hello" instead of "123".
#define STR(x)  #x
#define XSTR(x) STR(x)
#undef X
#define X 123

XSTR(X)
