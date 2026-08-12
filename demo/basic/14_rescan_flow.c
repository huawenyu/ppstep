// The "FIND MACRO" flowchart — one invocation per branch of this draw:
//
//                    FIND MACRO
//                        │
//                        ▼
//              ┌───────────────────┐
//              │ Macro disabled ?  │
//              └──────┬───────┬────┘
//                   yes│       │no
//                      │       │
//                      ▼       ▼
//                   DON'T    WHAT TYPE?
//                   EXPAND      │
//                               │
//                    ┌──────────┴──────────┐
//                    │                     │
//                    ▼                     ▼
//              OBJECT-LIKE           FUNCTION-LIKE
//                    │                     │
//                    │                     ▼
//                    │              collect arguments
//                    │                     │
//                    │                     ▼
//                    │              process arguments    ← (A) RAW vs EXPAND
//                    │                     │
//                    └──────────┬──────────┘
//                               │
//                               ▼
//                         SUBSTITUTION
//                               │
//                               ▼
//                             RESCAN                ← (B) what rescan hits
//                               │
//                               ▼
//                         FIND MACRO AGAIN
//
// This file walks the FUNCTION-LIKE branch (process arguments →
// substitution → rescan → find macro again), one case per sub-branch:
//
//   (A) process arguments — no  → EXPAND ARG: arg macro-expanded first
//   (A) process arguments — yes → RAW ARG:     # / ## operand, not expanded
//   (B) RESCAN → FIND MACRO AGAIN — newly formed macro name (##) → EXPAND
//   (B) RESCAN → FIND MACRO AGAIN — existing tokens that expand  → EXPAND
//
// Run:  ppstep demo/14_rescan_flow.c
// Try:  b c ORD     b c SSTR   b c PASTE   b c PASTED   b c BODY
//       c           (continue to each breakpoint)
//       <Enter>     (repeat last — step each branch)

// ── (A) no  → EXPAND ARG ───────────────────────────────────────────
//   `x` is an ordinary parameter, so the argument `N` is macro-expanded
//   BEFORE substitution.  N → 42, then `x + N` substitutes to `42 + N`,
//   and the rescan expands the N that is part of the body too.
#define N        42
#define ORD(x)   x + N
ORD(N)
//   → 42 + 42

// ── (A) yes → RAW ARG, via # ────────────────────────────────────────
//   `x` is the operand of `#`, so the argument N is NOT expanded.
//   `#x` stringizes the raw token text → "N".  The string literal is
//   not rescanned internally, and the body has no other macros.
#define SSTR(x)  #x
SSTR(N)
//   → "N"      (NOT "42" — # suppressed arg expansion)

// ── (A) yes → RAW ARG, via ## ─────────────────────────────────────
//   `a` and `b` are operands of `##`, so they are NOT expanded.
//   The arguments are the literal tokens "NA" and "NB" (the macro
//   names themselves, not their values foo/bar).  `a ## b` pastes the
//   raw tokens → "NANB", which is not a defined macro → copied as-is.
#define PASTE(a,b)  a ## b
#define NA           foo
#define NB           bar
PASTE(NA, NB)
//   → NANB     (NA/NB were NOT expanded to foo/bar because ## raws the
//                args; the pasted token NANB is rescanned but isn't a
//                macro, so it is copied)

// ── (B) newly formed macro name (## → rescan → EXPAND) ──────────────
//   Same RAW-arg ## path, but the paste produces the NAME of a defined
//   macro.  The arguments are the literal tokens "WRAP" and "ME" (not
//   macros), so `a ## b` → "WRAPME".  The rescan sees the newly-formed
//   token WRAPME and expands it to 7.  This is the "newly formed macro
//   name? → yes → EXPAND" branch.
#define WRAPME       7
#define PASTED(a,b)  a ## b
PASTED(WRAP, ME)
//   → 7        (WRAP ## ME synthesized `WRAPME`; the rescan expanded it)

// ── (B) existing tokens that can expand (ordinary rescan) ───────────
//   No # / ## here: the argument `N` is expanded normally (→ 42), AND
//   the body contains a macro name `SRC` that is found on the rescan
//   pass and expanded.  This is the "existing tokens that can expand?
//   → yes → EXPAND" branch.
#define SRC         99
#define BODY(x)     x + SRC
BODY(N)
//   → 42 + 99  (N → 42 as the expanded arg; SRC → 99 on rescan of body)
