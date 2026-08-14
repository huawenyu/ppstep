// Branch 3c' -- ## selects RAW args, the pasted token IS rescanned (and can be a macro)
//
//   └── function macro F(args) -> collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  -> splice RAW form
//                                   otherwise       -> splice EXPANDED form
//                                rescan(result, disabled ∪ {F})   <-- always runs!
//
// `##` makes its own use-site splice the RAW form, then pastes. The pasted
// result is part of the replacement list and is therefore rescanned. If
// pasting produces the name of a defined macro, that macro IS expanded.
//
//   #define F          bar
//   #define FOO        123
//   #define CAT(a,b)  a ## b, a
//   #define MAKE_FOO() CAT(F, OO)
//
//   MAKE_FOO()
//     |
//     | expand MAKE_FOO  -> body: CAT(F, OO)
//     | rescan CAT(F, OO):
//     |    expand CAT       prepare args: a raw="F" expanded="bar", b raw="OO" expanded="OO"
//     |                     body `a ## b, a` has TWO use-sites of a:
//     |                        `a ## b`  -> ## operand -> splice RAW a -> F ## OO -> FOO
//     |                        `, a`    -> ordinary    -> splice EXPANDED a -> bar
//     |    result so far: FOO, bar
//     |    rescan FOO, bar:
//     |       FOO is a macro -> 123 ; bar: not a macro -> copy
//     v
//   123, bar
//
// Two lessons in one body:
//   1. the paste synthesizes a NEW token (FOO) that did not exist in the
//      source, and the rescan expands it (-> 123) -- that's why rescan matters;
//   2. the SAME argument `a` is spliced TWICE in the body: RAW at the `##`
//      use-site (-> F), EXPANDED at the ordinary use-site (-> bar). This is
//      exactly the "two forms, one preparation" model.
//
// Run:  ppstep demo/basic/11_rescan_newmacro.c
// Try:  b c MAKE_FOO
//       b c CAT
//       b c FOO
//       c
//       s s s s s
//
// Trace:
//   1. calling MAKE_FOO  -- no args; body "CAT(F, OO)"
//   2. expanded MAKE_FOO -- substitute; rescan "CAT(F, OO)"
//   3. calling CAT        -- collect args a="F", b="OO"; prepare both forms
//                          (a raw="F" expanded="bar"; b raw="OO" expanded="OO")
//   4. expanded CAT       -- body `a ## b, a`:
//                          `a ## b` -> RAW a ## RAW b -> F ## OO -> FOO
//                          `, a`    -> EXPANDED a -> bar
//                          result: "FOO, bar"; rescan it
//   5. calling FOO        -- FOO is a macro (synthesized by the paste!)
//   6. expanded FOO      -- body "123"
//   7. rescanned FOO     -- "123": no macros -> copy
//   8. rescanned CAT     -- CAT's entry consumed
//   9. rescanned MAKE_FOO -- done
//
// Result:  123, bar

#define F          bar
#define FOO        123
#define CAT(a,b)   a ## b, a
#define MAKE_FOO() CAT(F, OO)

MAKE_FOO()
