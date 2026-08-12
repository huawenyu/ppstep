// Deep nested rescan — shows the rescan queue growing and shrinking
//
//   C → "B(42)"        → rescan queue: [B(42)]
//   B(42) → "A(42+1)"  → rescan queue: [A(42+1)]  (B popped, A pushed)
//   A(42+1) → "[42+1]" → rescan queue: [[42+1]]  (A popped, result pushed)
//   rescan [42+1]      → no macros → done
//   rescan A done       → pop A's entry
//   rescan B done        → pop B's entry
//   rescan C done         → pop C's entry → empty → fully expanded
//
// Run:  ppstep demo/07_deep_nesting.c
// Try:  b c C
//       b c B
//       b c A
//       c             (continue to first breakpoint: calling C)
//       s             (step to next macro event)
//       <Enter>       (repeat s — watch the call stack + rescan queue)
//
// The frames log (/tmp/ppstep_frames.log) shows both:
//   - call stack (expanding): which macro is being substituted RIGHT NOW
//   - rescan queue (rescanning): bodies waiting to be re-scanned
//
// Full event trace:
//   1. calling C        call stack: [C]
//   2. expanded C        call stack: []  rescan queue: [B(42)]
//   3. calling B         call stack: [B] rescan queue: [B(42)]
//   4. expanded B        call stack: []  rescan queue: [A(42+1)]
//   5. calling A         call stack: [A] rescan queue: [A(42+1)]
//   6. expanded A        call stack: []  rescan queue: [[42+1]]
//   7. rescanned A       rescan queue: [B(42)]  (A's entry consumed)
//   8. rescanned B       rescan queue: [C]      (B's entry consumed)
//   9. rescanned C       rescan queue: []       (C's entry consumed — done)
//
// Result:  [ 42 + 1 ]

#define A(x)  [x]
#define B(y)  A(y+1)
#define C     B(42)

C
