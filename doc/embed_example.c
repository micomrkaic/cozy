/* embed_example.c — Cozy as the numerical engine of a C program.
 *
 * Solves the three-equation New Keynesian model with the dsge package and
 * reads the decision rule straight into C arrays. Build and run from the
 * repository root:
 *
 *     make lib
 *     cc -I. doc/embed_example.c build/libcozy.a -lm -o embed_example
 *     ./embed_example
 */
#include <stdio.h>
#include "cozy.h"

int main(void)
{
    cozy_open();
    cozy_eval("load(\"dsge\")");                 /* finds packages/dsge.cz from the repository root */
    cozy_eval("let s = dsge.solve(dsge.model(dsge.nk()), {phi_pi = 1.5});"
              "let P = s.P; let Q = s.Q;");
    if (!cozy_ok()) { fputs(cozy_output(), stderr); return 1; }

    int n, k, rows, cols;
    const double *P = cozy_matrix("P", &n, &cols);     /* y = P y(-1) + Q e, row-major */
    const double *Q = cozy_matrix("Q", &rows, &k);
    printf("cozy %s: %d variables, %d shocks\n", cozy_version(), n, k);
    printf("inflation's response to a policy shock: %.6f\n", Q[1 * k + 1]);
    printf("persistence of the natural rate:        %.6f\n", P[3 * n + 3]);

    /* a question with a text answer comes back as the REPL would print it */
    printf("with phi_pi = 0.9 the model is %s", cozy_eval("dsge.bk(dsge.model(dsge.nk()), {phi_pi = 0.9}).verdict"));
    return 0;
}
