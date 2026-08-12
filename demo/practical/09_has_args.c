// Demo 9: HAS_ARGS — detecting end of argument list
#define SECOND(a, b, ...) b
#define IS_PROBE(...) SECOND(__VA_ARGS__, 0)
#define PROBE() ~, 1
#define CAT(a,b) a ## b
#define NOT(x) IS_PROBE(CAT(_NOT_, x))
#define _NOT_0 PROBE()
#define BOOL(x) NOT(NOT(x))

#define FIRST(a, ...) a
#define HAS_ARGS(...) BOOL(FIRST(_END_OF_ARGUMENTS_ __VA_ARGS__)())
#define _END_OF_ARGUMENTS_() 0

HAS_ARGS()
HAS_ARGS(some, arguments, here)
