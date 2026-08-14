// Pitfall — argument collection happens BEFORE argument expansion
//
//   F(a, b)   →  error: macro "F" passed 2 arguments, but takes just 1
//   F((a, b)) →  ( a , b )          (one arg — comma inside parens)
//   F(PAIR)   →  a , b              (ONE arg PAIR, expanded to "a, b")
//
// Commas that appear via MACRO EXPANSION do NOT change the original argument
// count. `F` collects its arguments FIRST (sees one arg `PAIR`), THEN expands
// that argument (`PAIR` → `a, b`). The generated comma is inside the expanded
// text of the single argument — it is not a separator between two arguments.
//
//   WRONG mental model:   expand first  → PAIR → a, b → collect 2 args
//   RIGHT mental model:   collect args  → [PAIR] → expand → [a, b] → substitute
//
// Run:  ppstep demo/basic/18_prepare_args.c
// Try:  b c F ; c ; s     (count args first, then expand the single arg)
//
// Trace (F(PAIR)):
//   1. calling  F          — collect ONE arg: x = raw "PAIR"
//      ── argument count already fixed at 1 ──
//   2. calling  PAIR       — expand the arg PAIR → "a, b" (EXPANDED form)
//   3. expanded F          — body x splices EXPANDED → a , b
//   4. rescanned F          — a , b: no macros → copy
//   Result:  a , b
//
// Note: F(a, b) is a compile-time error (too many args), shown as a warning.

#define F(x)    x
#define PAIR    a, b

F((a, b))
F(PAIR)
