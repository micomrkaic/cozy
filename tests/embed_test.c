/* embed_test.c — the embedding API (cozy.h), checked from C. Compiled and
 * run by tests/run_embed.sh as part of `make test`. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cozy.h"

static int fails;
#define CHECK(cond, what) do { if (!(cond)) { fails++; printf("embed FAIL: %s\n", what); } } while (0)

int main(void)
{
    int r, c; double x;
    CHECK(cozy_matrix("anything", &r, &c) == NULL && r == 0 && c == 0, "reading before the session opens gives nothing");
    cozy_open(); cozy_open();                                   /* twice is harmless */
    CHECK(strlen(cozy_version()) >= 5, "the version is reported");

    /* text out, as the REPL prints it */
    CHECK(!strcmp(cozy_eval("2 + 3 * 4"), "14\n") && cozy_ok(), "an expression echoes its value");
    CHECK(!strcmp(cozy_eval("let a = 5;"), "") && cozy_ok(), "a suppressed statement prints nothing");
    CHECK(!strcmp(cozy_output(), ""), "cozy_output repeats the last result");
    CHECK(!strcmp(cozy_eval("a * 2"), "10\n"), "bindings persist across calls");
    CHECK(!strcmp(cozy_eval("let f = fn x -> x^2;\nlet g = fn x -> f(x) + 1;\ng(3)"), "10\n"), "several lines run as one program");

    /* errors are reported, and the session survives them */
    cozy_eval("undefined_name + 1");
    CHECK(!cozy_ok() && strstr(cozy_output(), "undefined name"), "a runtime error is flagged and described");
    cozy_eval("let = ");
    CHECK(!cozy_ok() && strstr(cozy_output(), "parse error"), "a parse error is flagged and described");
    CHECK(!strcmp(cozy_eval("a + 1"), "6\n") && cozy_ok(), "the session carries on after an error");

    /* numbers back, without printing */
    cozy_eval("let M = [1.5, 2; 3, 4.25]; let v = [1; 2; 3]; let n = 7; let h = 0.5; let t = true; let msk = [1, 5] > 2; let s = \"text\";");
    const double *M = cozy_matrix("M", &r, &c);
    CHECK(M && r == 2 && c == 2 && M[0] == 1.5 && M[1] == 2 && M[2] == 3 && M[3] == 4.25, "a float matrix arrives row-major");
    const double *v = cozy_matrix("v", &r, &c);
    CHECK(v && r == 3 && c == 1 && v[0] == 1 && v[2] == 3, "an integer column arrives as doubles");
    CHECK(M[3] == 4.25, "an earlier read is still valid beside a later one");
    const double *one = cozy_matrix("h", &r, &c);
    CHECK(one && r == 1 && c == 1 && one[0] == 0.5, "a scalar reads as 1 x 1");
    const double *mask = cozy_matrix("msk", &r, &c);
    CHECK(mask && c == 2 && mask[0] == 0 && mask[1] == 1, "a logical array reads as zeros and ones");
    CHECK(cozy_scalar("n", &x) && x == 7 && cozy_scalar("h", &x) && x == 0.5 && cozy_scalar("t", &x) && x == 1, "scalars read directly");
    CHECK(!cozy_scalar("M", &x) && !cozy_scalar("nope", &x), "a matrix or a missing name is not a scalar");
    CHECK(cozy_matrix("s", &r, &c) == NULL && cozy_matrix("nope", &r, &c) == NULL && r == 0, "text and missing names are not matrices");
    CHECK(cozy_string("s") && !strcmp(cozy_string("s"), "text") && cozy_string("M") == NULL, "strings read as strings");
    cozy_eval("let z = [1+2i, 3];");
    CHECK(cozy_matrix("z", &r, &c) == NULL, "a complex array is refused, not truncated");

    /* a package, by load and as text carried in the program */
    cozy_eval("load(\"dsge\")");
    CHECK(cozy_ok(), "load finds the dsge package from the repository root");
    cozy_eval("let sol = dsge.solve(dsge.model(dsge.nk())); let Q = sol.Q; let P = sol.P;");
    const double *Q = cozy_matrix("Q", &r, &c);
    double lam = 1 / ((1 - 0.99 * 0.5) * (1.0 * (1 - 0.5) + 0.125) + 0.1275 * (1.5 - 0.5));
    CHECK(cozy_ok() && Q && r == 5 && c == 2 && fabs(Q[1 * 2 + 1] + 0.1275 * lam) < 1e-12, "a model solved in Cozy reads back as Gali's closed form");
    CHECK(!strcmp(cozy_eval("% a package as text\nlet twice = fn x -> 2 * x;\n% its second function\nlet thrice = fn x -> 3 * x;\ntwice(thrice(1))"), "6\n"),
          "a file's text, comments and all, evaluates in one call");
    for (int i = 0; i < 2000; i++) cozy_eval("let big = zeros(40, 40) + 1;");
    const double *big = cozy_matrix("big", &r, &c);
    CHECK(big && r == 40 && big[1599] == 1, "two thousand evaluations later the session is intact");

    if (!fails) printf("embed: 24 checks of the C API passed\n");
    return fails ? 1 : 0;
}
