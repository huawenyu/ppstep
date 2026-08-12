// Branch 3b' — # selects the RAW arg form, but the rest of the body IS rescanned
//
//   └── function macro F(args) → collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  → splice RAW form
//                                   otherwise       → splice EXPANDED form
//                                rescan(result, disabled ∪ {F})   ← always runs!
//
// `#` makes its own use-site splice the RAW form. It does NOT turn off the
// rescan of the substituted result. Any macro names that appear in the body
// OUTSIDE the stringized argument are still expanded on the rescan pass.
//
//   #define C           100
//   #define STR_PLUS(x) #x + C
//
//   STR_PLUS(C)
//     │
//     │ prepare x: raw="C", expanded="100"
//     │ #x splices RAW → stringize → "C"
//     │ plain C in the body is NOT a use of x — it's a literal body token
//     │ substitute body:  #x + C  →  "C" + C
//     │ rescan "C" + C:
//     │    "C"  → string-literal token, not rescanned internally
//     │     C   → macro C → 100
//     ▼
//   "C" + 100
//
// Contrast with the plain (no-#) case:
//
//   #define F(x)  x + C
//   F(C)  →  100 + 100        (x splices the EXPANDED form "100";
//                              body's C also expands on rescan → 100)
//
// Run:  ppstep demo/basic/10_rescan_existing.c
// Try:  b c STR_PLUS
//       b c C
//       c
//       s s s
//       i d        (STR_PLUS blue during its rescan; C only blue within C's own)
//
// Trace:
//   1. calling  STR_PLUS  — collect arg: x = raw "C"
//      ── argument preparation pre-pass runs here (STR_PLUS not yet blue) ──
//                            prepare: x raw="C", expanded="100"
//   2. expanded STR_PLUS  — #x splices raw → "C"; body's C is a literal token
//                            substitute: "C" + C  (body pushed to rescan queue)
//      ── rescan of STR_PLUS's body now runs; it finds `C` in the body ──
//   3. calling  C          — the body's C (NOT the arg) is a macro → expand
//   4. expanded C          — C → 100
//   5. rescanned C         — C's rescan done (innermost, popped first)
//   6. rescanned STR_PLUS  — STR_PLUS's rescan done
//
// Note the ordering: `rescanned STR_PLUS` fires LAST, not right after
// `expanded STR_PLUS`. The `rescanned` event marks the *completion* of a
// body's rescan; the nested `calling C → expanded C → rescanned C` chain
// in between IS that rescan in progress. ppstep has no separate
// "rescan-begin" event — `expanded` feeds straight into the rescan.
//
// Result:  "C" + 100
// The argument `C` took its RAW form (stringized → "C"); the body's literal
// `C` was NOT a use of x and WAS expanded on the rescan pass → 100.

#define C           100
#define STR_PLUS(x) #x + C

STR_PLUS(C)
