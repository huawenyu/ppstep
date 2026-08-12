// Branch 3c' — ## selects RAW args, the pasted token IS rescanned (and can be a macro)
//
//   └── function macro F(args) → collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  → splice RAW form
//                                   otherwise       → splice EXPANDED form
//                                rescan(result, disabled ∪ {F})   ← always runs!
//
// `##` makes its own use-site splice the RAW form, then pastes. The pasted
// result is part of the replacement list and is therefore rescanned. If
// pasting produces the name of a defined macro, that macro IS expanded.
//
//   #define FOO        123
//   #define CAT(a,b)   a ## b
//   #define MAKE_FOO() CAT(F, OO)
//
//   MAKE_FOO()
//     │
//     │ expand MAKE_FOO  → body: CAT(F, OO)
//     │ rescan CAT(F, OO):
//     │    expand CAT       prepare args: a raw="F" expanded="F", b raw="OO" expanded="OO"
//     │                     (F/OO aren't macros, so raw == expanded here)
//     │    ## use-site splices RAW → paste F ## OO → FOO
//     │    rescan FOO:
//     │       FOO is a macro → 123
//     ▼
//   123
//
// This is WHY the rescan matters: the paste synthesizes a token that did
// not exist as a single token in the source, and that token then expands.
//
// Run:  ppstep demo/basic/11_rescan_newmacro.c
// Try:  b c MAKE_FOO
//       b c CAT
//       b c FOO
//       c
//       s s s s s
//
// Trace:
//   1. calling MAKE_FOO  — no args; body "CAT(F, OO)"
//   2. expanded MAKE_FOO — substitute; rescan "CAT(F, OO)"
//   3. calling CAT       — collect args a="F", b="OO"; prepare both forms
//                          ## use-site → splice RAW forms
//   4. expanded CAT      — paste F ## OO → "FOO"; rescan "FOO"
//   5. calling FOO        — FOO is a macro
//   6. expanded FOO       — body "123"
//   7. rescanned FOO      — "123": no macros → copy
//   8. rescanned CAT      — CAT's entry consumed
//   9. rescanned MAKE_FOO — done
//
// Result:  123

#define FOO        123
#define CAT(a,b)   a ## b
#define MAKE_FOO() CAT(F, OO)

MAKE_FOO()
