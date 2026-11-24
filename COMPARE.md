# Cozy beside NumPy and Octave

Seven small tasks, each written three times: in Cozy, in Python with NumPy
(and SciPy where NumPy has no counterpart), and in Octave. The claim is not
that Cozy is shorter everywhere. For plain array arithmetic it is about as
terse as Octave, whose syntax it inherits. The claim is narrower and, we
think, more interesting: there are things the notation of mathematics says
directly that Cozy also says directly, and in which the other two are
either wrong, or refuse, or ask you to do the calculus yourself.

**The rules.** Each side is written the way a fluent user of that language
would write it, not as a straw man. Every line shown was run, and the
output printed under it is what came back:

- the Cozy transcripts are verified by `make test`, like every transcript
  in the manual;
- the Python and Octave transcripts are verified by `make compare`
  (tools/compare_check.py), which needs NumPy, SciPy and Octave and was
  last run with NumPy 2.5.3, SciPy 1.18.1 and Octave 8.4.0.

Only Octave was run on the MATLAB side. Where a remark below turns on
Octave's semantics rather than its syntax, MATLAB's documentation
describes the same behaviour, but it was not checked here.

## Contents

1. [A chained comparison](#1-a-chained-comparison)
2. [A column minus a row](#2-a-column-minus-a-row)
3. [A sum, as it is written](#3-a-sum-as-it-is-written)
4. [Describe a sample, count its outliers](#4-describe-a-sample-count-its-outliers)
5. [The Greeks, without deriving them](#5-the-greeks-without-deriving-them)
6. [A logistic regression and its standard errors](#6-a-logistic-regression-and-its-standard-errors)
7. [A derivative, as a formula](#7-a-derivative-as-a-formula)
8. [Speed](#speed)
9. [Where Cozy does not win](#where-cozy-does-not-win)

## 1. A chained comparison

*What share of a sample lies between −1.96 and 1.96?* Mathematics writes
the condition as −1.96 < z < 1.96.

```
cozy> let z = [-2.5, -1.2, 0.3, 0.8, 1.7, 2.2];
cozy> mean(-1.96 < z < 1.96)
0.666667
```

```python
>>> import numpy as np
>>> z = np.array([-2.5, -1.2, 0.3, 0.8, 1.7, 2.2])
>>> np.mean(-1.96 < z < 1.96)
Traceback (most recent call last):
    ...
ValueError: The truth value of an array with more than one element is ambiguous. Use a.any() or a.all()
>>> print(np.mean((-1.96 < z) & (z < 1.96)))
0.6666666666666666
```

```octave
>> z = [-2.5, -1.2, 0.3, 0.8, 1.7, 2.2];
>> disp(mean(-1.96 < z < 1.96))
1
>> disp(mean(-1.96 < z & z < 1.96))
0.6667
```

Python chains comparisons too, but on scalars only: on an array the chain
is an error, and the condition has to be taken apart and joined with `&`.
Octave accepts the chain and answers 1. It compares `-1.96 < z` first,
which gives zeros and ones, and every zero and one is below 1.96. Four of
the six numbers are in the interval; Octave says all of them are, and says
it without a warning.

In Cozy a chain means what it means on paper: both comparisons, joined
elementwise, with the middle term evaluated once.

## 2. A column minus a row

*The mean squared error of a fit.* The observations are a column; the
fitted values, which here equal them exactly, came back from somewhere as a
row. The error is zero.

```
cozy> let y = [1; 2; 3]; let fitted = [1, 2, 3];
cozy> mean((y - fitted) .^ 2)
error: shape mismatch: 3x1 vs 1x3
cozy> mean((y - fitted') .^ 2)
0
```

```python
>>> y = np.array([[1.0], [2.0], [3.0]]); fitted = np.array([1.0, 2.0, 3.0])
>>> print(np.mean((y - fitted) ** 2))
1.3333333333333333
>>> (y - fitted).shape
(3, 3)
```

```octave
>> y = [1; 2; 3]; fitted = [1, 2, 3];
>> disp(mean(mean((y - fitted) .^ 2)))
1.3333
>> disp(size(y - fitted))
   3   3
```

NumPy and Octave both stretch the two vectors against each other into a
3×3 table of every observation minus every fitted value, and report the
mean of its squares: 1.33 for a perfect fit, a plausible-looking number
with nothing to say it is wrong. Cozy broadcasts a scalar against an array
and nothing else; two arrays must have the same shape, and when they do not
the error names both shapes.

This is a choice with a cost. Broadcasting is how NumPy writes an outer
difference in one expression, and Cozy makes you say `repmat` or write the
sum. The examples in this document are the ones where saying so is
cheaper than finding the 1.33.

## 3. A sum, as it is written

*The price of a ten-year bond paying 5 a year, at a yield of 4%*: the sum
of c / (1 + y)^t for t from 1 to n, plus 100 / (1 + y)^n. And a double
sum: 1 / (i + j − 1) over i and j from 1 to 5, the entries of a Hilbert
matrix.

```
cozy> (sum[t = 1:n] c / (1 + y) ^ t) + 100 / (1 + y) ^ n where c = 5, y = 0.04, n = 10
108.111
cozy> sum[i = 1:5] sum[j = 1:5] 1 / (i + j - 1)
6.45635
```

```python
>>> c, y, n = 5, 0.04, 10
>>> t = np.arange(1, n + 1)
>>> print(np.sum(c / (1 + y) ** t) + 100 / (1 + y) ** n)
108.1108957...
>>> i = np.arange(1, 6)
>>> print(np.sum(1 / (i[:, None] + i[None, :] - 1)))
6.4563492...
```

```octave
>> c = 5; y = 0.04; n = 10; t = 1:n;
>> printf("%.3f\n", sum(c ./ (1 + y) .^ t) + 100 / (1 + y) ^ n)
108.111
>> i = 1:5;
>> printf("%.5f\n", sum(sum(1 ./ (i' + i - 1))))
6.45635
```

Octave is as short as Cozy here, and nobody would call either of the other
two versions hard. The difference is in what has to be translated. The
array versions turn the index into a vector, the operators into their
dotted forms, and the double sum into a column added to a row (the very
stretching that example 2 was about). `sum[t = 1:n]` keeps the index an
index; the body is the summand as written. Any function that reduces can
stand where `sum` does: `prod`, `max`, `mean`, or one of your own.

`where` gives the constants after the formula, as a textbook does, and
they do not outlive the expression.

## 4. Describe a sample, count its outliers

*Five summary statistics of a sample, by name; then the number of
observations more than two standard deviations from the mean.*

```
cozy> let x = [2.1, 3.7, 1.4, 5.0, 4.4, 2.9, 14.0, 3.3];
cozy> x |> {n = length, mu = mean, sd = std, lo = min, hi = max}
{n = 8, mu = 4.6, sd = 3.97205, lo = 1.4, hi = 14}
cozy> x |> (@ - mean(@)) / std(@) |> abs |> (@ > 2) |> sum
1
```

```python
>>> x = np.array([2.1, 3.7, 1.4, 5.0, 4.4, 2.9, 14.0, 3.3])
>>> {"n": len(x), "mu": x.mean(), "sd": x.std(ddof=1), "lo": x.min(), "hi": x.max()}
{'n': 8, 'mu': np.float64(4.6000000000000005), 'sd': np.float64(3.972045173099477), 'lo': np.float64(1.4), 'hi': np.float64(14.0)}
>>> print(np.sum(np.abs((x - x.mean()) / x.std(ddof=1)) > 2))
1
```

```octave
>> x = [2.1, 3.7, 1.4, 5.0, 4.4, 2.9, 14.0, 3.3];
>> disp(struct("n", numel(x), "mu", mean(x), "sd", std(x), "lo", min(x), "hi", max(x)))
    n = 8
    mu = 4.6000
    sd = 3.9720
    lo = 1.4000
    hi = 14
>> disp(sum(abs((x - mean(x)) / std(x)) > 2))
1
```

A pipe into a record applies each field's function to the value piped in:
the sample is named once, not five times. The second line reads in the
order things happen (centre, scale, take absolute values, compare, count),
where the nested calls read from the inside out.

A pipeline can also be watched. `|>>` is `|>` that prints what flows
through it, so one keystroke shows an intermediate value without taking
the expression apart:

```
cozy> [3, 1, 4, 1, 5, 9, 2, 6] |> sort |>> diff |> max
[1, 1, 2, 3, 4, 5, 6, 9]
3
```

(`ddof=1` in the NumPy lines is not decoration: `x.std()` alone divides by
n, and gives 3.72 where the other two give 3.97.)

## 5. The Greeks, without deriving them

*The Black–Scholes price of a call, and its sensitivities to the stock
price, to volatility and to time.*

In Cozy the price is written once and differentiated. A dual number
carries a value and a derivative through every operation exactly, so
`d(f)` is the derivative of `f` as a function: no step size, no
differencing, and nothing worked out by hand.

```
cozy> load("packages/autodiff.cz")
cozy> let N = fn x -> 0.5 * erfc(-x / sqrt(2));
cozy> let call = fn S, K, r, v, T -> S * N(d1) - K * exp(-r * T) * N(d1 - v * sqrt(T)) where d1 = (log(S / K) + (r + v ^ 2 / 2) * T) / (v * sqrt(T));
cozy> call(100, 105, 0.03, 0.2, 0.5)
4.1783
cozy> d(fn S -> call(S, 105, 0.03, 0.2, 0.5))(100)         # delta
0.433204
cozy> d(fn v -> call(100, 105, 0.03, v, 0.5))(0.2)         # vega
27.8131
cozy> -d(fn T -> call(100, 105, 0.03, 0.2, T))(0.5)        # theta
-6.73689
cozy> hdual12(call(hdual(100, 1, 1), 105, 0.03, 0.2, 0.5))  # gamma: the second derivative in S
0.0278131
```

In the other two the price is a function and each Greek is a formula of
its own, derived on paper and typed in:

```python
>>> from scipy.special import erfc
>>> N = lambda x: 0.5 * erfc(-x / np.sqrt(2))
>>> n = lambda x: np.exp(-x ** 2 / 2) / np.sqrt(2 * np.pi)
>>> def call(S, K, r, v, T):
...     d1 = (np.log(S / K) + (r + v ** 2 / 2) * T) / (v * np.sqrt(T))
...     return S * N(d1) - K * np.exp(-r * T) * N(d1 - v * np.sqrt(T))
>>> S, K, r, v, T = 100, 105, 0.03, 0.2, 0.5
>>> print(call(S, K, r, v, T))
4.17829...
>>> d1 = (np.log(S / K) + (r + v ** 2 / 2) * T) / (v * np.sqrt(T)); d2 = d1 - v * np.sqrt(T)
>>> print(N(d1))                                                                    # delta
0.433204...
>>> print(S * n(d1) * np.sqrt(T))                                                   # vega
27.8131...
>>> print(-S * n(d1) * v / (2 * np.sqrt(T)) - r * K * np.exp(-r * T) * N(d2))       # theta
-6.73689...
>>> print(n(d1) / (S * v * np.sqrt(T)))                                             # gamma
0.0278131...
```

```octave
>> N = @(x) 0.5 * erfc(-x / sqrt(2)); n = @(x) exp(-x .^ 2 / 2) / sqrt(2 * pi);
>> S = 100; K = 105; r = 0.03; v = 0.2; T = 0.5;
>> d1 = (log(S / K) + (r + v ^ 2 / 2) * T) / (v * sqrt(T)); d2 = d1 - v * sqrt(T);
>> printf("%.4f\n", S * N(d1) - K * exp(-r * T) * N(d2))
4.1783
>> printf("%.6f\n", N(d1))
0.433204
>> printf("%.4f\n", S * n(d1) * sqrt(T))
27.8131
>> printf("%.5f\n", -S * n(d1) * v / (2 * sqrt(T)) - r * K * exp(-r * T) * N(d2))
-6.73689
>> printf("%.7f\n", n(d1) / (S * v * sqrt(T)))
0.0278131
```

The three agree to every digit shown, which is the point: the closed
forms are what `d` computes. But the closed forms exist only because this
is the best-known formula in finance. Change the payoff, or the model, and
the Cozy lines stay as they are while the others go back to paper (or to a
finite difference and the choice of a step).

Automatic differentiation is not unique to Cozy. JAX and autograd do it
for Python; neither is NumPy, and both ask for their own array type or
their own copy of the library. In Cozy the dual number is a kind of
number, next to the complex one.

## 6. A logistic regression and its standard errors

*Twenty students, the hours each studied and whether each passed. Fit the
probability of passing as a logistic function of hours, with standard
errors.* The estimate maximises a likelihood; the standard errors are the
square roots of the diagonal of the inverse of its Hessian at the maximum.

```
cozy> load("packages/optim.cz")
cozy> let hours = [0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 1.75, 2.00, 2.25, 2.50, 2.75, 3.00, 3.25, 3.50, 4.00, 4.25, 4.50, 4.75, 5.00, 5.50]';
cozy> let pass = [0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1]';
cozy> let X = [ones(20, 1), hours];
cozy> let nll = fn b -> sum(log(1 + exp(X * b)) - pass .* (X * b));
cozy> let fit = minimize_newton(nll, [0.0; 0.0]);
cozy> fit.x
[-4.07771; 1.50465]
cozy> hess(nll)(fit.x) |> inv |> diag |> sqrt
[1.76099; 0.628721]
```

The negative log-likelihood is the only thing written down. Newton's
method takes its gradient and its Hessian from the function itself, and
the last line takes the Hessian again for the standard errors. These are
the textbook values for this data set: −4.0777 and 1.5046, with standard
errors 1.7610 and 0.6287.

SciPy's `minimize` will also work from the function alone. It then
estimates the gradient by finite differences and returns, as `hess_inv`,
the approximation to the inverse Hessian that the BFGS iteration happened
to build:

```python
>>> from scipy.optimize import minimize
>>> hours = np.array([0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 1.75, 2.00, 2.25, 2.50, 2.75, 3.00, 3.25, 3.50, 4.00, 4.25, 4.50, 4.75, 5.00, 5.50])
>>> passed = np.array([0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1])
>>> X = np.column_stack([np.ones(20), hours])
>>> nll = lambda b: np.sum(np.log1p(np.exp(X @ b)) - passed * (X @ b))
>>> fit = minimize(nll, np.zeros(2))
>>> print(np.round(fit.x, 3), np.round(np.sqrt(np.diag(fit.hess_inv)), 3))
[-4.078  1.505] [1.751 0.629]
```

The coefficients are right and the first standard error is not: 1.751
where the answer is 1.761. To get the answer the gradient and the Hessian
of the likelihood have to be derived and supplied:

```python
>>> def grad(b):
...     p = 1 / (1 + np.exp(-X @ b))
...     return X.T @ (p - passed)
>>> def hess(b):
...     p = 1 / (1 + np.exp(-X @ b))
...     return X.T @ (X * (p * (1 - p))[:, None])
>>> fit = minimize(nll, np.zeros(2), jac=grad, hess=hess, method="Newton-CG")
>>> print(np.round(fit.x, 3), np.round(np.sqrt(np.diag(np.linalg.inv(hess(fit.x)))), 3))
[-4.078  1.505] [1.761 0.629]
```

Octave's `fminunc` returns a Hessian as its sixth output. It, too, is an
approximation, and here both standard errors come out low:

```octave
>> hours = [0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 1.75, 2.00, 2.25, 2.50, 2.75, 3.00, 3.25, 3.50, 4.00, 4.25, 4.50, 4.75, 5.00, 5.50]';
>> pass = [0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1]';
>> X = [ones(20, 1), hours];
>> nll = @(b) sum(log(1 + exp(X * b)) - pass .* (X * b));
>> [b, fval, info, out, g, H] = fminunc(nll, [0; 0]);
>> printf("%.3f %.3f\n", b)
-4.078 1.505
>> printf("%.2f %.2f\n", sqrt(diag(inv(H))))
1.75 0.62
>> p = 1 ./ (1 + exp(-X * b)); H = X' * (X .* (p .* (1 - p)));
>> printf("%.2f %.2f\n", sqrt(diag(inv(H))))
1.76 0.63
```

| | standard errors | what you supply |
|---|---|---|
| Cozy | 1.761, 0.629 | the likelihood |
| SciPy, as it comes | 1.751, 0.629 | the likelihood |
| Octave `fminunc` | 1.75, 0.62 | the likelihood |
| SciPy or Octave, exact | 1.761, 0.629 | the likelihood, its gradient and its Hessian |

A statistician would reach for statsmodels or `glmfit`, which have the
logistic model built in and get this right. The comparison is about what
happens one step off that path, with a likelihood that no library has
built in: the short way gives standard errors that are slightly wrong,
and the right way costs two derivations.

## 7. A derivative, as a formula

*The derivative of sin(x)/x, as an expression.*

```
cozy> load("packages/symb.cz")
cozy> deriv("sin(x)/x")
"((cos(x) / x) - (sin(x) / x^2))"
cozy> deriv("exp(-x^2)")
"(-2 * (exp((-x^2)) * x))"
```

Neither NumPy nor Octave differentiates a formula. Python does it with
SymPy, and Octave with its symbolic package, which runs SymPy underneath;
both are good, and far more capable than this. `symb.cz` is three hundred
lines of Cozy in the standard packages: expressions are records, the
derivative is a recursion over them, and the result can be handed back to
the numerical side as a function (`dfun("sin(x)/x")` goes straight into
`fzero`).

## Speed

None of the above is bought with speed. Built on OpenBLAS (`make
BACKEND=openblas`; Accelerate on a Mac), Cozy hands its linear algebra to
the same library NumPy and Octave use, and since 0.1.38 its own array
kernels run over raw numbers as theirs do. Seconds, best of three, on one
four-core machine, with NumPy 2.5.3 and Octave 8.4.0:

| | Cozy 0.1.38 | NumPy | Octave |
|---|---|---|---|
| `A * B`, n = 1000 | 0.016 | 0.007 | 0.011 |
| `A \ b`, n = 1000 | 0.021 | 0.009 | 0.015 |
| symmetric `eig` with vectors, n = 1000 | 0.13 | 0.08 | 1.03 |
| `svd` with vectors, n = 1000 | 0.26 | 0.27 | 2.09 |
| `sum(sin(x) .^ 2 + cos(x) .^ 2)`, 5 million elements | 0.14 | 0.08 | 0.17 |
| `sort(x)`, 5 million elements | 0.24 | 0.04 | 0.55 |
| a scalar loop of a million passes | 0.09 | 0.03 | 0.56 |

Unlike the transcripts, these numbers are not checked by any test: they
are one machine on one day, and the ratios are what to read. Dense
linear algebra is the library's speed in all three. Octave is slow on the
two decompositions because it calls LAPACK's older QR drivers when
vectors are asked for; Cozy and NumPy call the divide-and-conquer ones.
NumPy's sort and its elementwise loops are written with SIMD instructions
and are the ones to beat. In scalar loops Cozy sits between CPython and Octave.

## Where Cozy does not win

A page like this owes the reader the other column.

- **Plain array arithmetic.** `A \ b`, `x .^ 2`, `[1, 2; 3, 4]`: Cozy and
  Octave are the same language here, by inheritance. NumPy is wordier
  (`np.linalg.solve(A, b)`), and that is all there is to say.
- **More than two dimensions.** NumPy arrays have any number of axes, and
  its linear algebra works on stacks of matrices: the determinants of two
  thousand random 3×3 matrices are `np.linalg.det(rng.standard_normal((2000, 3, 3)))`.
  Cozy has scalars, vectors and matrices, and maps a function over the
  count instead: `1:2000 ~> (fn i -> det(randn(3)))`.
- **Broadcasting, when you mean it.** Example 2 shows the cost of
  stretching shapes silently. The benefit is real too: an outer difference
  is `y - fitted` in NumPy and Octave, on purpose.
- **Everything around the language.** Data frames, plotting libraries,
  file formats, GPUs, and a generation of answered questions. Cozy has
  fourteen packages; Python has the world.

None of this changes the seven examples. It says where they stop.
