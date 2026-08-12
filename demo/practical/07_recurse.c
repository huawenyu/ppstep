// Demo 7: Recursion via deferred expansion
#define EMPTY()
#define DEFER1(m) m EMPTY()
#define EVAL1(...) __VA_ARGS__

#define RECURSE() I am recursive, look: DEFER1(_RECURSE)()()
#define _RECURSE() RECURSE

// One pass: produces deferred _RECURSE
RECURSE()

// Two passes: recursion unfolds once more
EVAL1(RECURSE())

// Full EVAL for deeper recursion
#define EVAL(...) EVAL1024(__VA_ARGS__)
#define EVAL1024(...) EVAL512(EVAL512(__VA_ARGS__))
#define EVAL512(...) EVAL256(EVAL256(__VA_ARGS__))
#define EVAL256(...) EVAL128(EVAL128(__VA_ARGS__))
#define EVAL128(...) EVAL64(EVAL64(__VA_ARGS__))
#define EVAL64(...) EVAL32(EVAL32(__VA_ARGS__))
#define EVAL32(...) EVAL16(EVAL16(__VA_ARGS__))
#define EVAL16(...) EVAL8(EVAL8(__VA_ARGS__))
#define EVAL8(...) EVAL4(EVAL4(__VA_ARGS__))
#define EVAL4(...) EVAL2(EVAL2(__VA_ARGS__))
#define EVAL2(...) EVAL1(EVAL1(__VA_ARGS__))

EVAL(RECURSE())
