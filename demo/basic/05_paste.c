// Branch 3c -- ## selects the RAW form at its use-site (token paste)
//
//   CAT(NAME, h2)  ->  NAMEh2   (NOT wad_http_h2)
//
//   └── function macro F(args) -> collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  -> splice RAW form
//                                   otherwise       -> splice EXPANDED form
//                                rescan(result, disabled ∪ {F})
//
// The argument-preparation phase computes BOTH forms up front. `##` does
// NOT suppress that phase -- it only makes its own use-site splice the RAW
// form (then paste it) instead of the EXPANDED one. The pasted result IS
// rescanned for further macros; only the argument *form* differs.
//
// Run:  ppstep demo/basic/05_paste.c
// Try:  b c CAT    (break on call CAT)
//       c          (continue to the CAT call)
//       s          (step -- see "expanded CAT" -> NAMEh2)
//
// Trace:
//   1. calling CAT         -- collect args: x = raw "NAME", y = raw "h2"
//      -- argument preparation pre-pass runs here (CAT not yet blue) --
//                            prepare: x raw="NAME" expanded="wad_http_", y raw="h2"
//                            (NAME does expand -- that's the EXPANDED form --
//                             but the ## use-site splices the RAW form)
//   2. expanded CAT        -- x##y splices RAW forms -> NAME ## h2 -> NAMEh2
//                            result: "NAMEh2"
//   3. rescanned CAT       -- rescan "NAMEh2": not a macro -> copy
//
// Result:  NAMEh2
// The `##` use took the RAW form of x ("NAME"), so the expanded form
// ("wad_http_") was never spliced. The pasted token `NAMEh2` is rescanned,
// but since it isn't a defined macro, it stays as-is.

#define CAT(x,y)  x##y
#define NAME      wad_http_
CAT(NAME, h2)
