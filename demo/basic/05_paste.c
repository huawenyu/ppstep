// Branch 3c — ## selects the RAW form at its use-site (token paste)
//
//   └── function macro F(args) → collect arguments (raw)
//                                PREPARE each arg: store RAW form AND EXPANDED form
//                                walk body, per use-site:
//                                   # / ## operand  → splice RAW form
//                                   otherwise       → splice EXPANDED form
//                                rescan(result, disabled ∪ {F})
//
// The argument-preparation phase computes BOTH forms up front. `##` does
// NOT suppress that phase — it only makes its own use-site splice the RAW
// form (then paste it) instead of the EXPANDED one. The pasted result IS
// rescanned for further macros; only the argument *form* differs.
//
// Run:  ppstep demo/basic/05_paste.c
// Try:  b c CAT    (break on call CAT)
//       c          (continue to the CAT call)
//       s          (step — see "expanded CAT" → NAME_suffix)
//
// Trace:
//   1. calling CAT         — collect args: x = raw "NAME", y = raw "_suffix"
//      ── argument preparation pre-pass runs here (CAT not yet blue) ──
//                            prepare: x raw="NAME" expanded="var", y raw="_suffix"
//                            (NAME does expand — that's the EXPANDED form —
//                             but the ## use-site splices the RAW form)
//   2. expanded CAT        — x##y splices RAW forms → NAME ## _suffix → NAME_suffix
//                            result: "NAME_suffix"
//   3. rescanned CAT       — rescan "NAME_suffix": not a macro → copy
//
// Result:  NAME_suffix
// The `##` use took the RAW form of x ("NAME"), so the expanded form
// ("var") was never spliced. The pasted token `NAME_suffix` is rescanned,
// but since it isn't a defined macro, it stays as-is.

#define CAT(x,y)  x##y
#define NAME      var
CAT(NAME, _suffix)
