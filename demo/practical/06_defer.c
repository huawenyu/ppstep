// Demo 6: DEFER — delaying macro expansion to the next pass
#define EMPTY()
#define DEFER1(m) m EMPTY()

#define B(n) n is my favourite!

// DEFER1 prevents B from expanding in this pass
DEFER1(B)(321)

// But EVAL forces the second pass where it does expand
#define EVAL1(...) __VA_ARGS__
EVAL1(DEFER1(B)(321))
