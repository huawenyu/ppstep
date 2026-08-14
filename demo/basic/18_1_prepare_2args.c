// prepare-args — demo 1: two args (error)
//
//   F(a, b)   →  error: macro "F" passed 2 arguments, but takes just 1
//
// `F` takes one parameter `x`. A top-level comma in the call Splits the
// arguments: `F(a, b)` is TWO arguments, so it is a compile-time error.
//
// Run:  ppstep demo/basic/18_1_prepare_2args.c
// Try:  b c F ; c        (break on call F, then continue — see the error)

#define F(x)    x

F(a, b)
