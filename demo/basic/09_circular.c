// Branch 2b — mutual / circular self-reference: the disabled set stops the loop
//
//   ├── object macro M → substitute replacement
//                        rescan(replacement, disabled ∪ {M})
//
// When two macros reference each other, naive expansion would never
// terminate:
//
//     A → B → A → B → A → ...   (∞)
//
// The C standard prevents this with the "temporarily disabled" rule:
// while rescanning macro M's body, M itself is disabled.  So the chain
// stops as soon as a disabled name is re-encountered.
//
// Run:  ppstep demo/09_circular.c
// Try:  b c A
//       b c B
//       c
//       s s s s
//
// Trace (two macros, A ↔ B):
//   1. calling A        — look up #define A B
//   2. expanded A       — body "B" produced; disabled = {A}; rescan "B"
//   3. calling B        — look up #define B A
//   4. expanded B       — body "A" produced; disabled = {A,B}; rescan "A"
//   5. rescanned B      — "A" is disabled → copy literally (no re-expansion)
//   6. rescanned A      — done
//
// Result:  A
// The inner `A` is left verbatim because it was in the disabled set when
// its own body was being rescanned.
//
// ---
// Three-level cycle: A → B → C → A
//
//   #define A B
//   #define B C
//   #define C A
//
//   A
//     │ expand A   disabled = {A}        → B
//     │ expand B   disabled = {A,B}      → C
//     │ expand C   disabled = {A,B,C}    → A
//     │ A is disabled → leave unchanged
//     ▼
//   A
//
// Each macro is disabled only for the duration of ITS OWN rescan, so the
// cycle unwinds as soon as the name that started it is seen again.

#define A B
#define B A
A

#define P Q
#define Q R
#define R P
P
