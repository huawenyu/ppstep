// Branch 3a — function macro: parse args, EXPAND args, substitute, rescan
//
//   └── function macro F(args) → parse arguments
//                                expand arguments        ← unless # or ##
//                                substitute
//                                rescan(result, disabled ∪ {F})
//
// Without # or ##, the argument is fully macro-expanded BEFORE being
// substituted into the macro body.
//
// Run:  ppstep demo/03_funcargs.c
// Try:  b c ID    (break on call ID)
//       b c M     (break on call M)
//       c         (continue — stops at ID call, then step through)
//
// Trace:
//   1. calling ID          — parse arg: x = "M"
//                             expand arg: M is a macro
//   2. calling M            — M expands to "42"
//   3. expanded M           — arg is now "42"
//   4. expanded ID          — substitute body "x" → "42"
//   5. rescanned ID         — rescan "42": no macros → done
//
// Result:  42
// `M` was expanded to `42` before substitution into ID's body.

#define ID(x)    x
#define M        42
ID(M)
