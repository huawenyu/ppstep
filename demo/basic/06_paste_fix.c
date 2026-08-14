// Fix -- expand the argument BEFORE pasting via a second macro level.
//
//   CAT_(NAME, h2)   ->  NAMEh2         (pitfall: ## uses the RAW form)
//   CAT (NAME, h2)   ->  wad_http_h2    (fix: expand NAME first)
//
// `CAT_` does the raw `##` paste (the trap): it pastes the raw text of its
// operands, so `NAME ## h2` -> `NAMEh2` (the un-expanded NAME).
// `CAT` is a thin wrapper with an ORDINARY param, so its arguments expand
// first (NAME -> wad_http_) before the body `CAT_(x,y)` runs. By the time
// `##` sees them, the operand is already the expanded value, so the paste
// produces `wad_http_h2`. The user just calls `CAT` and gets the right
// answer.
//
// Run:  ppstep demo/basic/06_paste_fix.c
// Try:  b c CAT  ; c ; s s   (expand NAME -> wad_http_, then paste)
//       b c CAT_ ; c ; s s   (the inner CAT_ pastes the expanded value)
//
// Trace (CAT):
//   1. calling  CAT         -- args: x=NAME, y=h2 (raw)
//      -- ordinary params -> splice EXPANDED: NAME -> wad_http_ --
//   2. expanded CAT         -- body CAT_(x,y) -> CAT_(wad_http_, h2)
//   3. rescanned CAT        -- CAT_(wad_http_, h2): FUNCTION-LIKE
//   4. calling  CAT_         -- args: a=wad_http_, b=h2 (raw)
//   5. expanded CAT_        -- a ## b -> wad_http_ ## h2 -> wad_http_h2
//   Result:  wad_http_h2

#define CAT_(x,y)  x##y
#define CAT(x,y)   CAT_(x,y)
#define NAME       wad_http_

CAT_(NAME, h2)
CAT(NAME, h2)
