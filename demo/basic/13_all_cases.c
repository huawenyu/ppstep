// All cases — one file per branch of the expansion flowchart
//
//   expand(tokens, disabled)
//       │
//       ├── ordinary token              ← case 1
//       │      └── copy to output as-is
//       │
//       ├── object macro M              ← case 2, 3
//       │      ├── substitute (M added to disabled)
//       │      └── rescan(replacement, disabled ∪ {M})
//       │             └── self-ref M stays literal ← "paint-blue"
//       │             └── mutual ref A↔B stops the loop ← disabled set
//       │
//       └── function macro F(args)      ← case 4, 5, 6
//              ├── parse arguments
//              ├── expand arguments     ← UNLESS # or ##
//              │      └── ordinary param: arg expanded    ← case 4
//              │      └── # param:       raw → stringize  ← case 5
//              │      └── ## param:      raw → paste      ← case 6
//              ├── substitute (F added to disabled)
//              └── rescan(result, disabled ∪ {F})
//                     └── result may contain more macros ← cases 4,5,6
//
// This file collects one invocation per case so you can single-step
// through each branch of the flowchart in a single session.
//
// Run:  ppstep demo/13_all_cases.c
// Try:  b c ID    b c STR   b c CAT   b c MAKE_FOO
//       b c A     b c B     b c C
//       c         (continue to each breakpoint in turn)
//       <Enter>   (repeat last command — step through each case)
//
// Expected final output (in source order):
//
//   case 1  ordinary token:        hello X world      (X not a macro)
//   case 2  object macro:         3.14159            (PI substituted)
//   case 3  object macro self-ref: foo + 1           (foo painted blue)
//   case 3' mutual self-ref:      A                  (A↔B stops; inner A literal)
//   case 4  function macro:       42                 (arg M expanded before subst)
//   case 4' nested rescan:        [99]               (arg B → 99, result rescanned)
//   case 5  # (stringize):        "X"                (raw arg, not "hello")
//   case 5' # + body rescan:       "C" + 100         (# raws arg, body C rescanned)
//   case 6  ## (paste):           NAME_suffix        (raw args, not var_suffix)
//   case 6' ## raw + rescan:      XY                 (X not expanded; XY not a macro)
//   case 6'' ## paste → macro:    123                (FOO ## OO → FOO → 123)

// ---- case 1: ordinary token (no macro, just copy) ----------------------
#define GREETING hello
GREETING X world

// ---- case 2: object-like macro (substitute + rescan) -------------------
#define PI 3.14159
PI

// ---- case 3: object macro self-reference (paint-blue) ------------------
#define foo foo + 1
foo

// ---- case 3': mutual self-reference (disabled set stops the loop) -------
#define A B
#define B A
A

// ---- case 4: function macro, ordinary arg (expand arg before subst) ----
#define ID(x)   x
#define M       42
ID(M)

// ---- case 4': function macro, nested rescan of the result --------------
#define NA(x)   [x]
#define NB      99
NA(NB)

// ---- case 5: # stringize (raw arg → string literal) -------------------
#define STR(x)  #x
#define X       hello
STR(X)

// ---- case 5': # raws arg, but body's C still rescans ------------------
#define NC           100
#define STR_PLUS(x)  #x + C
// (re-#define C below so this case is self-contained)
#undef C
#define C           100
STR_PLUS(C)

// ---- case 6: ## token paste (raw args, pasted result rescanned) --------
#define CAT(a,b)    a ## b
#define NAME        var
CAT(NAME, _suffix)

// ---- case 6': ## with macro args (args stay RAW, not pre-expanded) -----
#define NX          123
#define NCAT(a,b)   a ## b
// (X already #defined as hello above; redefine as 123 for this case)
#undef X
#define X           123
NCAT(X, Y)

// ---- case 6'': ## paste produces a macro name that re-expands ---------
#define FOO         123
#define PCAT(a,b)   a ## b
#define MAKE_FOO()  PCAT(F, OO)
MAKE_FOO()
