// Branch 3d — rescan finds a macro in the SUBSTITUTED BODY (not the argument)
//
//   └── function macro F(args) → ...
//                                substitute
//                                rescan(result, disabled ∪ {F})
//
// After substitution, the result is RESCANNED.  If the macro BODY
// contains a macro name (one that is NOT an argument), that name is
// found and expanded on the rescan pass — even though the argument
// itself had no macros in it.
//
// This is distinct from:
//   - 03_funcargs.c  — the ARGUMENT is a macro, expanded before subst
//   - 09_circular.c  — object macros cycle, disabled set stops the loop
//
// Run:  ppstep demo/06_nested_rescan.c
// Try:  b c A
//       b c TAIL
//       c
//       s s s s
//
// Trace:
//   1. calling A       — parse arg: x = "42"
//                         (42 is not a macro → arg stays "42")
//   2. expanded A      — substitute body "x + TAIL" → "42 + TAIL"
//   3. calling TAIL    — rescan "42 + TAIL": TAIL is a macro
//   4. expanded TAIL   — TAIL → "99"
//   5. rescanned TAIL  — "42 + 99": no macros → done
//   6. rescanned A     — done
//
// Result:  42 + 99
// `TAIL` was not in the argument — it was part of A's body, and the
// rescan pass found and expanded it.  The argument `42` contributed
// nothing macro-like; the rescan did all the work.

#define A(x)    x + TAIL
#define TAIL    99
A(42)
