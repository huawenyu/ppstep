// prepare-args — demo 2: resolve demo 1's error with parentheses
//
//   F((a, b)) →  ( a , b )     (ONE arg — comma inside parens)
//
// Wrap the comma in parentheses and it is no longer a top-level separator:
// `F((a, b))` is ONE argument `( a, b )`, so `F` accepts it.
//
// Run:  ppstep demo/basic/18_2_prepare_parens.c
// Try:  b c F ; c ; s   (one arg collected, then substituted)

#define F(x)    x

F((a, b))
