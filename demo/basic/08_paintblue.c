// Paint-blue across two macros — the disabled set is per-rescan-scope
//
// A macro is disabled during the rescan of ITS OWN body.  But if the
// macro's body calls ANOTHER macro, and that other macro's body
// references the first one — it IS expandable there (different scope).
//
// Run:  ppstep demo/08_paintblue.c
// Try:  b c WRAP
//       b c X
//       c
//       s s s s
//       i d            (show the painted-blue / disabled set at each step)
//
// Source:
//   #define X      X_value      ← X's body contains X (self-ref, painted blue)
//   #define WRAP(x) x           ← WRAP just returns its arg
//   WRAP(X)                     ← X is expanded FIRST (arg expansion),
//                                  then WRAP(X_value) is substituted
//   X                           ← X is expanded again (fresh scope, not disabled)
//
// Trace for WRAP(X):
//   1. calling WRAP       — parse arg: x = "X"
//                            expand arg: X is a macro
//   2. calling X           — X expands to "X_value"
//                            BUT: X is in disabled set during X's own rescan
//                            so the "X" inside "X_value" is NOT re-expanded
//   3. expanded X          — arg is now "X_value"
//   4. expanded WRAP       — substitute body "x" → "X_value"
//   5. rescanned WRAP       — rescan "X_value": not a macro → copy
//
// Result of WRAP(X):  X_value
//
// Then the standalone `X` on the next line:
//   6. calling X           — fresh expansion, X is NOT disabled
//   7. expanded X          — "X_value", rescan with X disabled
//   8. rescanned X         — "X_value" has no macros → copy
//
// Result:  X_value  (same output, but X WAS expandable — not painted blue)

#define X      X_value
#define WRAP(x) x
WRAP(X)
X
