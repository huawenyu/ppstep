// Demo 3: NOT, BOOL, and the CAT trick
#define SECOND(a, b, ...) b
#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1

#define CAT(a,b) a ## b

#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()

#define BOOL(x) NOT(NOT(x))

NOT(0)
NOT(42)
BOOL(0)
BOOL(123)
