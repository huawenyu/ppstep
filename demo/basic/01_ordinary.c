// Branch 1 — ordinary token → copy (no macro, no call, no rescan)
//
//   expand(tokens, disabled)
//       │
//       ├── ordinary token → copy to output as-is
//
// Run:  ppstep demo/01_ordinary.c
// Try:  s   (step — skips lexed tokens, stops at macro events)
//       c   (continue — runs to end, no breakpoints set)
//
// Expected output:  hello X world
// `X` is not a defined macro, so it is copied verbatim — no `calling`/`expanded`
// event fires for it.  Only `lexed` events appear for these tokens.

#define GREETING hello
GREETING X world
