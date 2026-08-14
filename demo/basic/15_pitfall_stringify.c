// Pitfall — the classic `#` trap (stringify uses the RAW argument)
//
//   STR(FOO)  →  "FOO"   (NOT "100")
//
// `#` stringizes the *raw* argument — argument expansion is suppressed for an
// operand of `#`. So `FOO` is NOT expanded to `100` before it is stringized;
// the literal text `FOO` becomes the string `"FOO"`.
//
// Run:  ppstep demo/basic/15_pitfall_stringify.c
// Try:  b c STR ; c ; s s   (see: arg collected raw, never expanded)
//
// Trace:
//   1. calling  STR        — collect arg: x = raw "FOO"
//      ── argument preparation: # operand → only RAW form used ──
//   2. expanded STR        — #x splices raw → "FOO"
//   3. rescanned STR       — "FOO": no macros → copy
//
// Result:  "FOO"

#define STR(x)  #x
#define FOO     100

STR(FOO)
