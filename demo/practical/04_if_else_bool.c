// Demo 4: IF_ELSE with BOOL — works with any non-zero value
#define SECOND(a, b, ...) b
#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1
#define CAT(a,b) a ## b
#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()
#define BOOL(x) NOT(NOT(x))

#define IF_ELSE(condition) _IF_ELSE(BOOL(condition))
#define _IF_ELSE(condition) CAT(_IF_, condition)
#define _IF_1(...) __VA_ARGS__ _IF_1_ELSE
#define _IF_0(...)             _IF_0_ELSE
#define _IF_1_ELSE(...)
#define _IF_0_ELSE(...) __VA_ARGS__

IF_ELSE(123)(is non zero)(is zero)
IF_ELSE(0)(is non zero)(is zero)
IF_ELSE(1)(is non zero)(is zero)
