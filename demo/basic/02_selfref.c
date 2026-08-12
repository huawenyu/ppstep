// Branch 2 — object macro: substitute + rescan, with paint-blue (self-reference)
//
//   ├── object macro M → substitute replacement
//                        rescan(replacement, disabled ∪ {M})
//
// The macro name is added to the disabled set ("painted blue") BEFORE
// rescanning its own body.  This prevents infinite self-recursion: the
// inner `foo` is seen but NOT re-expanded.
//
// Run:  ppstep demo/02_selfref.c
// Try:  s   s   s   (step through calling → expanded → rescanned)
//
// Trace:
//   1. calling foo        — Wave sees `foo`, looks up #define foo foo + 1
//   2. expanded foo        — body "foo + 1" produced; rescan queue: [foo + 1]
//                           disabled set now includes `foo`
//   3. rescanned foo       — rescan "foo + 1" with `foo` disabled:
//                           `foo` → ordinary token (copy), `+ 1` → copy
//
// Result:  foo + 1
// The inner `foo` stays literal because it was in the disabled set.

#define foo foo + 1
foo
