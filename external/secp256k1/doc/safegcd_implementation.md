# The safegcd implementation in libsecp256k1 explained

This document explains the modular inverse and Jacobi symbol implementations in the `src/modinv*.h` files.
It is based on the paper
["Fast constant-time gcd computation and modular inversion"](https://gcd.cr.yp.to/papers.html#safegcd)
by Daniel J. Bernstein and Bo-Yin Yang. The references below are for the Date: 2019.04.13 version.

The actual implementation is in C of course, but for demonstration purposes Python3 is used here.
Most implementation aspects and optimizations are explained, except those that depend on the specific
number representation used in the C code.

## 1. Computing the Greatest Common Divisor (GCD) using divsteps

The algorithm from the paper (section 11), at a very high level, is this:

```python
def gcd(f, g):
    """Compute the GCD of an odd integer f and another integer g."""
    assert f & 1  # require f to be odd
    delta = 1     # additional state variable
    while g != 0:
        assert f & 1  # f will be odd in every iteration
        if delta > 0 and g & 1:
            delta, f, g = 1 - delta, g, (g - f) // 2
        elif g & 1:
            delta, f, g = 1 + delta, f, (g + f) // 2
        else:
            delta, f, g = 1 + delta, f, (g    ) // 2
    return abs(f)
```

It computes the greatest common divisor of an odd integer _f_ and any integer _g_. Its inner loop
keeps rewriting the variables _f_ and _g_ alongside a state variable _&delta;_ that starts at _1_, until
_g=0_ is reached. At that point, _|f|_ gives the GCD. Each of the transitions in the loop is called a
"division step" (referred to as divstep in what follows).

For example, _gcd(21, 14)_ would be computed as:

- Start with _&delta;=1 f=21 g=14_
- Take the third branch: _&delta;=2 f=21 g=7_
- Take the first branch: _&delta;=-1 f=7 g=-7_
- Take the second branch: _&delta;=0 f=7 g=0_
- The answer _|f| = 7_.

Why it works:

- Divsteps can be decomposed into two steps (see paragraph 8.2 in the paper):
  - (a) If _g_ is odd, replace _(f,g)_ with _(g,g-f)_ or (f,g+f), resulting in an even _g_.
  - (b) Replace _(f,g)_ with _(f,g/2)_ (where _g_ is guaranteed to be even).
- Neither of those two operations change the GCD:
  - For (a), assume _gcd(f,g)=c_, then it must be the case that _f=a&thinsp;c_ and _g=b&thinsp;c_ for some integers _a_
    and _b_. As _(g,g-f)=(b&thinsp;c,(b-a)c)_ and _(f,f+g)=(a&thinsp;c,(a+b)c)_, the result clearly still has
    common factor _c_. Reasoning in the other direction shows that no common factor can be added by
    doing so either.
  - For (b), we know that _f_ is odd, so _gcd(f,g)_ clearly has no factor _2_, and we can remove
    it from _g_.
- The algorithm will eventually converge to _g=0_. This is proven in the paper (see theorem G.3).
- It follows that eventually we find a final value _f'_ for which _gcd(f,g) = gcd(f',0)_. As the
  gcd of _f'_ and _0_ is _|f'|_ by definition, that is our answer.

Compared to more [traditional GCD algorithms](https://en.wikipedia.org/wiki/Euclidean_algorithm), this one has the property of only ever looking at
the low-order bits of the variables to decide the next steps, and being easy to make
constant-time (in more low-level languages than Python). The _&delta;_ parameter is necessary to
guide the algorithm towards shrinking the numbers' magnitudes without explicitly needing to look
at high order bits.

Properties that will become important later:

- Performing more divsteps than needed is not a problem, as _f_ does not change anymore after _g=0_.
- Only even numbers are divided by _2_. This means that when reasoning about it algebraically we
  do not need to worry about rounding.
- At every point during the algorithm's execution the next _N_ steps only depend on the bottom _N_
  bits of _f_ and _g_, and on _&delta;_.

## 2. From GCDs to modular inverses

We want an algorithm to compute the inverse _a_ of _x_ modulo _M_, i.e. the number a such that _a&thinsp;x=1
mod M_. This inverse only exists if the GCD of _x_ and _M_ is _1_, but that is always the case if _M_ is
prime and _0 < x < M_. In what follows, assume that the modular inverse exists.
It turns out this inverse can be computed as a side effect of computing the GCD by keeping track
of how the internal variables can be written as linear combinations of the inputs at every step
(see the [extended Euclidean algorithm](https://en.wikipedia.org/wiki/Extended_Euclidean_algorithm)).
Since the GCD is _1_, such an algorithm will compute numbers _a_ and _b_ such that a&thinsp;x + b&thinsp;M = 1*.
Taking that expression *mod M* gives *a&thinsp;x mod M = 1*, and we see that *a* is the modular inverse of *x
mod M\*.

A similar approach can be used to calculate modular inverses using the divsteps-based GCD
algorithm shown above, if the modulus _M_ is odd. To do so, compute _gcd(f=M,g=x)_, while keeping
track of extra variables _d_ and _e_, for which at every step _d = f/x (mod M)_ and _e = g/x (mod M)_.
_f/x_ here means the number which multiplied with _x_ gives _f mod M_. As _f_ and _g_ are initialized to _M_
and _x_ respectively, _d_ and _e_ just start off being _0_ (_M/x mod M = 0/x mod M = 0_) and _1_ (_x/x mod M
= 1_).

```python
def div2(M, x):
    """Helper routine to compute x/2 mod M (where M is odd)."""
    assert M & 1
    if x & 1: # If x is odd, make it even by adding M.
        x += M
    # x must be even now, so a clean division by 2 is possible.
    return x // 2

def modinv(M, x):
    """Compute the inverse of x mod M (given that it exists, and M is odd)."""
    assert M & 1
    delta, f, g, d, e = 1, M, x, 0, 1
    while g != 0:
        # Note that while division by two for f and g is only ever done on even inputs, this is
        # not true for d and e, so we need the div2 helper function.
        if delta > 0 and g & 1:
            delta, f, g, d, e = 1 - delta, g, (g - f) // 2, e, div2(M, e - d)
        elif g & 1:
            delta, f, g, d, e = 1 + delta, f, (g + f) // 2, d, div2(M, e + d)
        else:
            delta, f, g, d, e = 1 + delta, f, (g    ) // 2, d, div2(M, e    )
        # Verify that the invariants d=f/x mod M, e=g/x mod M are maintained.
        assert f % M == (d * x) % M
        assert g % M == (e * x) % M
    assert f == 1 or f == -1  # |f| is the GCD, it must be 1
    # Because of invariant d = f/x (mod M), 1/x = d/f (mod M). As |f|=1, d/f = d*f.
    return (d * f) % M
```

Also note that this approach to track _d_ and _e_ throughout the computation to determine the inverse
is different from the paper. There (see paragraph 12.1 in the paper) a transition matrix for the
entire computation is determined (see section 3 below) and the inverse is computed from that.
The approach here avoids the need for 2x2 matrix multiplications of various sizes, and appears to
be faster at the level of optimization we're able to do in C.

## 3. Batching multiple divsteps

Every divstep can be expressed as a matrix multiplication, applying a transition matrix _(1/2 t)_
to both vectors _[f, g]_ and _[d, e]_ (see paragraph 8.1 in the paper):

```
  t = [ u,  v ]
      [ q,  r ]

  [ out_f ] = (1/2 * t) * [ in_f ]
  [ out_g ] =             [ in_g ]

  [ out_d ] = (1/2 * t) * [ in_d ]  (mod M)
  [ out_e ]               [ in_e ]
```

where _(u, v, q, r)_ is _(0, 2, -1, 1)_, _(2, 0, 1, 1)_, or _(2, 0, 0, 1)_, depending on which branch is
taken. As above, the resulting _f_ and _g_ are always integers.

Performing multiple divsteps corresponds to a multiplication with the product of all the
individual divsteps' transition matrices. As each transition matrix consists of integers
divided by _2_, the product of these matrices will consist of integers divided by _2<sup>N</sup>_ (see also
theorem 9.2 in the paper). These divisions are expensive when updating _d_ and _e_, so we delay
them: we compute the integer coefficients of the combined transition matrix scaled by _2<sup>N</sup>_, and
do one division by _2<sup>N</sup>_ as a final step:

```python
def divsteps_n_matrix(delta, f, g):
    """Compute delta and transition matrix t after N divsteps (multiplied by 2^N)."""
    u, v, q, r = 1, 0, 0, 1 # start with identity matrix
    for _ in range(N):
        if delta > 0 and g & 1:
            delta, f, g, u, v, q, r = 1 - delta, g, (g - f) // 2, 2*q, 2*r, q-u, r-v
        elif g & 1:
            delta, f, g, u, v, q, r = 1 + delta, f, (g + f) // 2, 2*u, 2*v, q+u, r+v
        else:
            delta, f, g, u, v, q, r = 1 + delta, f, (g    ) // 2, 2*u, 2*v, q  , r
    return delta, (u, v, q, r)
```

As the branches in the divsteps are completely determined by the bottom _N_ bits of _f_ and _g_, this
function to compute the transition matrix only needs to see those bottom bits. Furthermore all
intermediate results and outputs fit in _(N+1)_-bit numbers (unsigned for _f_ and _g_; signed for _u_, _v_,
_q_, and _r_) (see also paragraph 8.3 in the paper). This means that an implementation using 64-bit
integers could set _N=62_ and compute the full transition matrix for 62 steps at once without any
big integer arithmetic at all. This is the reason why this algorithm is efficient: it only needs
to update the full-size _f_, _g_, _d_, and _e_ numbers once every _N_ steps.

We still need functions to compute:

```
  [ out_f ] = (1/2^N * [ u,  v ]) * [ in_f ]
  [ out_g ]   (        [ q,  r ])   [ in_g ]

  [ out_d ] = (1/2^N * [ u,  v ]) * [ in_d ]  (mod M)
  [ out_e ]   (        [ q,  r ])   [ in_e ]
```

Because the divsteps transformation only ever divides even numbers by two, the result of _t&thinsp;[f,g]_ is always even. When _t_ is a composition of _N_ divsteps, it follows that the resulting _f_
and _g_ will be multiple of _2<sup>N</sup>_, and division by _2<sup>N</sup>_ is simply shifting them down:

```python
def update_fg(f, g, t):
    """Multiply matrix t/2^N with [f, g]."""
    u, v, q, r = t
    cf, cg = u*f + v*g, q*f + r*g
    # (t / 2^N) should cleanly apply to [f,g] so the result of t*[f,g] should have N zero
    # bottom bits.
    assert cf % 2**N == 0
    assert cg % 2**N == 0
    return cf >> N, cg >> N
```

The same is not true for _d_ and _e_, and we need an equivalent of the `div2` function for division by _2<sup>N</sup> mod M_.
This is easy if we have precomputed _1/M mod 2<sup>N</sup>_ (which always exists for odd _M_):

```python
def div2n(M, Mi, x):
    """Compute x/2^N mod M, given Mi = 1/M mod 2^N."""
    assert (M * Mi) % 2**N == 1
    # Find a factor m such that m*M has the same bottom N bits as x. We want:
    #     (m * M) mod 2^N = x mod 2^N
    # <=> m mod 2^N = (x / M) mod 2^N
    # <=> m mod 2^N = (x * Mi) mod 2^N
    m = (Mi * x) % 2**N
    # Subtract that multiple from x, cancelling its bottom N bits.
    x -= m * M
    # Now a clean division by 2^N is possible.
    assert x % 2**N == 0
    return (x >> N) % M

def update_de(d, e, t, M, Mi):
    """Multiply matrix t/2^N with [d, e], modulo M."""
    u, v, q, r = t
    cd, ce = u*d + v*e, q*d + r*e
    return div2n(M, Mi, cd), div2n(M, Mi, ce)
```

With all of those, we can write a version of `modinv` that performs _N_ divsteps at once:

```python3
def modinv(M, Mi, x):
    """Compute the modular inverse of x mod M, given Mi=1/M mod 2^N."""
    assert M & 1
    delta, f, g, d, e = 1, M, x, 0, 1
    while g != 0:
        # Compute the delta and transition matrix t for the next N divsteps (this only needs
        # (N+1)-bit signed integer arithmetic).
        delta, t = divsteps_n_matrix(delta, f % 2**N, g % 2**N)
        # Apply the transition matrix t to [f, g]:
        f, g = update_fg(f, g, t)
        # Apply the transition matrix t to [d, e]:
        d, e = update_de(d, e, t, M, Mi)
    return (d * f) % M
```

This means that in practice we'll always perform a multiple of _N_ divsteps. This is not a problem
because once _g=0_, further divsteps do not affect _f_, _g_, _d_, or _e_ anymore (only _&delta;_ keeps
increasing). For variable time code such excess iterations will be mostly optimized away in later
sections.

## 4. Avoiding modulus operations

So far, there are two places where we compute a remainder of big numbers modulo _M_: at the end of
`div2n` in every `update_de`, and at the very end of `modinv` after potentially negating _d_ due to the
sign of _f_. These are relatively expensive operations when done generically.

To deal with the modulus operation in `div2n`, we simply stop requiring _d_ and _e_ to be in range
_[0,M)_ all the time. Let's start by inlining `div2n` into `update_de`, and dropping the modulus
operation at the end:

```python
def update_de(d, e, t, M, Mi):
    """Multiply matrix t/2^N with [d, e] mod M, given Mi=1/M mod 2^N."""
    u, v, q, r = t
    cd, ce = u*d + v*e, q*d + r*e
    # Cancel out bottom N bits of cd and ce.
    md = -((Mi * cd) % 2**N)
    me = -((Mi * ce) % 2**N)
    cd += md * M
    ce += me * M
    # And cleanly divide by 2**N.
    return cd >> N, ce >> N
```

Let's look at bounds on the ranges of these numbers. It can be shown that _|u|+|v|_ and _|q|+|r|_
never exceed _2<sup>N</sup>_ (see paragraph 8.3 in the paper), and thus a multiplication with _t_ will have
outputs whose absolute values are at most _2<sup>N</sup>_ times the maximum absolute input value. In case the
inputs _d_ and _e_ are in _(-M,M)_, which is certainly true for the initial values _d=0_ and _e=1_ assuming
_M > 1_, the multiplication results in numbers in range _(-2<sup>N</sup>M,2<sup>N</sup>M)_. Subtracting less than _2<sup>N</sup>_
times _M_ to cancel out _N_ bits brings that up to _(-2<sup>N+1</sup>M,2<sup>N</sup>M)_, and
dividing by _2<sup>N</sup>_ at the end takes it to _(-2M,M)_. Another application of `update_de` would take that
to _(-3M,2M)_, and so forth. This progressive expansion of the variables' ranges can be
counteracted by incrementing _d_ and _e_ by _M_ whenever they're negative:

```python
    ...
    if d < 0:
        d += M
    if e < 0:
        e += M
    cd, ce = u*d + v*e, q*d + r*e
    # Cancel out bottom N bits of cd and ce.
    ...
```

With inputs in _(-2M,M)_, they will first be shifted into range _(-M,M)_, which means that the
output will again be in _(-2M,M)_, and this remains the case regardless of how many `update_de`
invocations there are. In what follows, we will try to make this more efficient.

Note that increasing _d_ by _M_ is equal to incrementing _cd_ by _u&thinsp;M_ and _ce_ by _q&thinsp;M_. Similarly,
increasing _e_ by _M_ is equal to incrementing _cd_ by _v&thinsp;M_ and _ce_ by _r&thinsp;M_. So we could instead write:

```python
    ...
    cd, ce = u*d + v*e, q*d + r*e
    # Perform the equivalent of incrementing d, e by M when they're negative.
    if d < 0:
        cd += u*M
        ce += q*M
    if e < 0:
        cd += v*M
        ce += r*M
    # Cancel out bottom N bits of cd and ce.
    md = -((Mi * cd) % 2**N)
    me = -((Mi * ce) % 2**N)
    cd += md * M
    ce += me * M
    ...
```

Now note that we have two steps of corrections to _cd_ and _ce_ that add multiples of _M_: this
increment, and the decrement that cancels out bottom bits. The second one depends on the first
one, but they can still be efficiently combined by only computing the bottom bits of _cd_ and _ce_
at first, and using that to compute the final _md_, _me_ values:

```python
def update_de(d, e, t, M, Mi):
    """Multiply matrix t/2^N with [d, e], modulo M."""
    u, v, q, r = t
    md, me = 0, 0
    # Compute what multiples of M to add to cd and ce.
    if d < 0:
        md += u
        me += q
    if e < 0:
        md += v
        me += r
    # Compute bottom N bits of t*[d,e] + M*[md,me].
    cd, ce = (u*d + v*e + md*M) % 2**N, (q*d + r*e + me*M) % 2**N
    # Correct md and me such that the bottom N bits of t*[d,e] + M*[md,me] are zero.
    md -= (Mi * cd) % 2**N
    me -= (Mi * ce) % 2**N
    # Do the full computation.
    cd, ce = u*d + v*e + md*M, q*d + r*e + me*M
    # And cleanly divide by 2**N.
    return cd >> N, ce >> N
```

One last optimization: we can avoid the _md&thinsp;M_ and _me&thinsp;M_ multiplications in the bottom bits of _cd_
and _ce_ by moving them to the _md_ and _me_ correction:

```python
    ...
    # Compute bottom N bits of t*[d,e].
    cd, ce = (u*d + v*e) % 2**N, (q*d + r*e) % 2**N
    # Correct md and me such that the bottom N bits of t*[d,e]+M*[md,me] are zero.
    # Note that this is not the same as {md = (-Mi * cd) % 2**N} etc. That would also result in N
    # zero bottom bits, but isn't guaranteed to be a reduction of [0,2^N) compared to the
    # previous md and me values, and thus would violate our bounds analysis.
    md -= (Mi*cd + md) % 2**N
    me -= (Mi*ce + me) % 2**N
    ...
```

The resulting function takes _d_ and _e_ in range _(-2M,M)_ as inputs, and outputs values in the same
range. That also means that the _d_ value at the end of `modinv` will be in that range, while we want
a result in _[0,M)_. To do that, we need a normalization function. It's easy to integrate the
conditional negation of _d_ (based on the sign of _f_) into it as well:

```python
def normalize(sign, v, M):
    """Compute sign*v mod M, where v is in range (-2*M,M); output in [0,M)."""
    assert sign == 1 or sign == -1
    # v in (-2*M,M)
    if v < 0:
        v += M
    # v in (-M,M). Now multiply v with sign (which can only be 1 or -1).
    if sign == -1:
        v = -v
    # v in (-M,M)
    if v < 0:
        v += M
    # v in [0,M)
    return v
```

And calling it in `modinv` is simply:

```python
   ...
   return normalize(f, d, M)
```

## 5. Constant-time operation

The primary selling point of the algorithm is fast constant-time operation. What code flow still
depends on the input data so far?

- the number of iterations of the while _g &ne; 0_ loop in `modinv`
- the branches inside `divsteps_n_matrix`
- the sign checks in `update_de`
- the sign checks in `normalize`

To make the while loop in `modinv` constant time it can be replaced with a constant number of
iterations. The paper proves (Theorem 11.2) that _741_ divsteps are sufficient for any _256_-bit
inputs, and [safegcd-bounds](https://github.com/sipa/safegcd-bounds) shows that the slightly better bound _724_ is
sufficient even. Given that every loop iteration performs _N_ divsteps, it will run a total of
_&lceil;724/N&rceil;_ times.

To deal with the branches in `divsteps_n_matrix` we will replace them with constant-time bitwise
operations (and hope the C compiler isn't smart enough to turn them back into branches; see
`ctime_tests.c` for automated tests that this isn't the case). To do so, observe that a
divstep can be written instead as (compare to the inner loop of `gcd` in section 1).

```python
    x = -f if delta > 0 else f         # set x equal to (input) -f or f
    if g & 1:
        g += x                         # set g to (input) g-f or g+f
        if delta > 0:
            delta = -delta
            f += g                     # set f to (input) g (note that g was set to g-f before)
    delta += 1
    g >>= 1
```

To convert the above to bitwise operations, we rely on a trick to negate conditionally: per the
definition of negative numbers in two's complement, (_-v == ~v + 1_) holds for every number _v_. As
_-1_ in two's complement is all _1_ bits, bitflipping can be expressed as xor with _-1_. It follows
that _-v == (v ^ -1) - (-1)_. Thus, if we have a variable _c_ that takes on values _0_ or _-1_, then
_(v ^ c) - c_ is _v_ if _c=0_ and _-v_ if _c=-1_.

Using this we can write:

```python
    x = -f if delta > 0 else f
```

in constant-time form as:

```python
    c1 = (-delta) >> 63
    # Conditionally negate f based on c1:
    x = (f ^ c1) - c1
```

To use that trick, we need a helper mask variable _c1_ that resolves the condition _&delta;>0_ to _-1_
(if true) or _0_ (if false). We compute _c1_ using right shifting, which is equivalent to dividing by
the specified power of _2_ and rounding down (in Python, and also in C under the assumption of a typical two's complement system; see
`assumptions.h` for tests that this is the case). Right shifting by _63_ thus maps all
numbers in range _[-2<sup>63</sup>,0)_ to _-1_, and numbers in range _[0,2<sup>63</sup>)_ to _0_.

Using the facts that _x&0=0_ and _x&(-1)=x_ (on two's complement systems again), we can write:

```python
    if g & 1:
        g += x
```

as:

```python
    # Compute c2=0 if g is even and c2=-1 if g is odd.
    c2 = -(g & 1)
    # This masks out x if g is even, and leaves x be if g is odd.
    g += x & c2
```

Using the conditional negation trick again we can write:

```python
    if g & 1:
        if delta > 0:
            delta = -delta
```

as:

```python
    # Compute c3=-1 if g is odd and delta>0, and 0 otherwise.
    c3 = c1 & c2
    # Conditionally negate delta based on c3:
    delta = (delta ^ c3) - c3
```

Finally:

```python
    if g & 1:
        if delta > 0:
            f += g
```

becomes:

```python
    f += g & c3
```

It turns out that this can be implemented more efficiently by applying the substitution
_&eta;=-&delta;_. In this representation, negating _&delta;_ corresponds to negating _&eta;_, and incrementing
_&delta;_ corresponds to decrementing _&eta;_. This allows us to remove the negation in the _c1_
computation:

```python
    # Compute a mask c1 for eta < 0, and compute the conditional negation x of f:
    c1 = eta >> 63
    x = (f ^ c1) - c1
    # Compute a mask c2 for odd g, and conditionally add x to g:
    c2 = -(g & 1)
    g += x & c2
    # Compute a mask c for (eta < 0) and odd (input) g, and use it to conditionally negate eta,
    # and add g to f:
    c3 = c1 & c2
    eta = (eta ^ c3) - c3
    f += g & c3
    # Incrementing delta corresponds to decrementing eta.
    eta -= 1
    g >>= 1
```

A variant of divsteps with better worst-case performance can be used instead: starting _&delta;_ at
_1/2_ instead of _1_. This reduces the worst case number of iterations to _590_ for _256_-bit inputs
(which can be shown using convex hull analysis). In this case, the substitution _&zeta;=-(&delta;+1/2)_
is used instead to keep the variable integral. Incrementing _&delta;_ by _1_ still translates to
decrementing _&zeta;_ by _1_, but negating _&delta;_ now corresponds to going from _&zeta;_ to _-(&zeta;+1)_, or
_~&zeta;_. Doing that conditionally based on _c3_ is simply:

```python
    ...
    c3 = c1 & c2
    zeta ^= c3
    ...
```

By replacing the loop in `divsteps_n_matrix` with a variant of the divstep code above (extended to
also apply all _f_ operations to _u_, _v_ and all _g_ operations to _q_, _r_), a constant-time version of
`divsteps_n_matrix` is obtained. The full code will be in section 7.

These bit fiddling tricks can also be used to make the conditional negations and additions in
`update_de` and `normalize` constant-time.

## 6. Variable-time optimizations

In section 5, we modified the `divsteps_n_matrix` function (and a few others) to be constant time.
Constant time operations are only necessary when computing modular inverses of secret data. In
other cases, it slows down calculations unnecessarily. In this section, we will construct a
faster non-constant time `divsteps_n_matrix` function.

To do so, first consider yet another way of writing the inner loop of divstep operations in
`gcd` from section 1. This decomposition is also explained in the paper in section 8.2. We use
the original version with initial _&delta;=1_ and _&eta;=-&delta;_ here.

```python
for _ in range(N):
    if g & 1 and eta < 0:
        eta, f, g = -eta, g, -f
    if g & 1:
        g += f
    eta -= 1
    g >>= 1
```

Whenever _g_ is even, the loop only shifts _g_ down and decreases _&eta;_. When _g_ ends in multiple zero
bits, these iterations can be consolidated into one step. This requires counting the bottom zero
bits efficiently, which is possible on most platforms; it is abstracted here as the function
`count_trailing_zeros`.

```python
def count_trailing_zeros(v):
    """
    When v is zero, consider all N zero bits as "trailing".
    For a non-zero value v, find z such that v=(d<<z) for some odd d.
    """
    if v == 0:
        return N
    else:
        return (v & -v).bit_length() - 1

i = N # divsteps left to do
while True:
    # Get rid of all bottom zeros at once. In the first iteration, g may be odd and the following
    # lines have no effect (until "if eta < 0").
    zeros = min(i, count_trailing_zeros(g))
    eta -= zeros
    g >>= zeros
    i -= zeros
    if i == 0:
        break
    # We know g is odd now
    if eta < 0:
        eta, f, g = -eta, g, -f
    g += f
    # g is even now, and the eta decrement and g shift will happen in the next loop.
```

We can now remove multiple bottom _0_ bits from _g_ at once, but still need a full iteration whenever
there is a bottom _1_ bit. In what follows, we will get rid of multiple _1_ bits simultaneously as
well.

Observe that as long as _&eta; &geq; 0_, the loop does not modify _f_. Instead, it cancels out bottom
bits of _g_ and shifts them out, and decreases _&eta;_ and _i_ accordingly - interrupting only when _&eta;_
becomes negative, or when _i_ reaches _0_. Combined, this is equivalent to adding a multiple of _f_ to
_g_ to cancel out multiple bottom bits, and then shifting them out.

It is easy to find what that multiple is: we want a number _w_ such that _g+w&thinsp;f_ has a few bottom
zero bits. If that number of bits is _L_, we want _g+w&thinsp;f mod 2<sup>L</sup> = 0_, or _w = -g/f mod 2<sup>L</sup>_. Since _f_
is odd, such a _w_ exists for any _L_. _L_ cannot be more than _i_ steps (as we'd finish the loop before
doing more) or more than _&eta;+1_ steps (as we'd run `eta, f, g = -eta, g, -f` at that point), but
apart from that, we're only limited by the complexity of computing _w_.

This code demonstrates how to cancel up to 4 bits per step:

```python
NEGINV16 = [15, 5, 3, 9, 7, 13, 11, 1] # NEGINV16[n//2] = (-n)^-1 mod 16, for odd n
i = N
while True:
    zeros = min(i, count_trailing_zeros(g))
    eta -= zeros
    g >>= zeros
    i -= zeros
    if i == 0:
        break
    # We know g is odd now
    if eta < 0:
        eta, f, g = -eta, g, -f
    # Compute limit on number of bits to cancel
    limit = min(min(eta + 1, i), 4)
    # Compute w = -g/f mod 2**limit, using the table value for -1/f mod 2**4. Note that f is
    # always odd, so its inverse modulo a power of two always exists.
    w = (g * NEGINV16[(f & 15) // 2]) % (2**limit)
    # As w = -g/f mod (2**limit), g+w*f mod 2**limit = 0 mod 2**limit.
    g += w * f
    assert g % (2**limit) == 0
    # The next iteration will now shift out at least limit bottom zero bits from g.
```

By using a bigger table more bits can be cancelled at once. The table can also be implemented
as a formula. Several formulas are known for computing modular inverses modulo powers of two;
some can be found in Hacker's Delight second edition by Henry S. Warren, Jr. pages 245-247.
Here we need the negated modular inverse, which is a simple transformation of those:

- Instead of a 3-bit table:
  - _-f_ or _f ^ 6_
- Instead of a 4-bit table:
  - _1 - f(f + 1)_
  - _-(f + (((f + 1) & 4) << 1))_
- For larger tables the following technique can be used: if _w=-1/f mod 2<sup>L</sup>_, then _w(w&thinsp;f+2)_ is
  _-1/f mod 2<sup>2L</sup>_. This allows extending the previous formulas (or tables). In particular we
  have this 6-bit function (based on the 3-bit function above):
  - _f(f<sup>2</sup> - 2)_

This loop, again extended to also handle _u_, _v_, _q_, and _r_ alongside _f_ and _g_, placed in
`divsteps_n_matrix`, gives a significantly faster, but non-constant time version.

## 7. Final Python version

All together we need the following functions:

- A way to compute the transition matrix in constant time, using the `divsteps_n_matrix` function
  from section 2, but with its loop replaced by a variant of the constant-time divstep from
  section 5, extended to handle _u_, _v_, _q_, _r_:

```python
def divsteps_n_matrix(zeta, f, g):
    """Compute zeta and transition matrix t after N divsteps (multiplied by 2^N)."""
    u, v, q, r = 1, 0, 0, 1 # start with identity matrix
    for _ in range(N):
        c1 = zeta >> 63
        # Compute x, y, z as conditionally-negated versions of f, u, v.
        x, y, z = (f ^ c1) - c1, (u ^ c1) - c1, (v ^ c1) - c1
        c2 = -(g & 1)
        # Conditionally add x, y, z to g, q, r.
        g, q, r = g + (x & c2), q + (y & c2), r + (z & c2)
        c1 &= c2                     # reusing c1 here for the earlier c3 variable
        zeta = (zeta ^ c1) - 1       # inlining the unconditional zeta decrement here
        # Conditionally add g, q, r to f, u, v.
        f, u, v = f + (g & c1), u + (q & c1), v + (r & c1)
        # When shifting g down, don't shift q, r, as we construct a transition matrix multiplied
        # by 2^N. Instead, shift f's coefficients u and v up.
        g, u, v = g >> 1, u << 1, v << 1
    return zeta, (u, v, q, r)
```

- The functions to update _f_ and _g_, and _d_ and _e_, from section 2 and section 4, with the constant-time
  changes to `update_de` from section 5:

```python
def update_fg(f, g, t):
    """Multiply matrix t/2^N with [f, g]."""
    u, v, q, r = t
    cf, cg = u*f + v*g, q*f + r*g
    return cf >> N, cg >> N

def update_de(d, e, t, M, Mi):
    """Multiply matrix t/2^N with [d, e], modulo M."""
    u, v, q, r = t
    d_sign, e_sign = d >> 257, e >> 257
    md, me = (u & d_sign) + (v & e_sign), (q & d_sign) + (r & e_sign)
    cd, ce = (u*d + v*e) % 2**N, (q*d + r*e) % 2**N
    md -= (Mi*cd + md) % 2**N
    me -= (Mi*ce + me) % 2**N
    cd, ce = u*d + v*e + M*md, q*d + r*e + M*me
    return cd >> N, ce >> N
```

- The `normalize` function from section 4, made constant time as well:

```python
def normalize(sign, v, M):
    """Compute sign*v mod M, where v in (-2*M,M); output in [0,M)."""
    v_sign = v >> 257
    # Conditionally add M to v.
    v += M & v_sign
    c = (sign - 1) >> 1
    # Conditionally negate v.
    v = (v ^ c) - c
    v_sign = v >> 257
    # Conditionally add M to v again.
    v += M & v_sign
    return v
```

- And finally the `modinv` function too, adapted to use _&zeta;_ instead of _&delta;_, and using the fixed
  iteration count from section 5:

```python
def modinv(M, Mi, x):
    """Compute the modular inverse of x mod M, given Mi=1/M mod 2^N."""
    zeta, f, g, d, e = -1, M, x, 0, 1
    for _ in range((590 + N - 1) // N):
        zeta, t = divsteps_n_matrix(zeta, f % 2**N, g % 2**N)
        f, g = update_fg(f, g, t)
        d, e = update_de(d, e, t, M, Mi)
    return normalize(f, d, M)
```

- To get a variable time version, replace the `divsteps_n_matrix` function with one that uses the
  divsteps loop from section 5, and a `modinv` version that calls it without the fixed iteration
  count:

```python
NEGINV16 = [15, 5, 3, 9, 7, 13, 11, 1] # NEGINV16[n//2] = (-n)^-1 mod 16, for odd n
def divsteps_n_matrix_var(eta, f, g):
    """Compute eta and transition matrix t after N divsteps (multiplied by 2^N)."""
    u, v, q, r = 1, 0, 0, 1
    i = N
    while True:
        zeros = min(i, count_trailing_zeros(g))
        eta, i = eta - zeros, i - zeros
        g, u, v = g >> zeros, u << zeros, v << zeros
        if i == 0:
            break
        if eta < 0:
            eta, f, u, v, g, q, r = -eta, g, q, r, -f, -u, -v
        limit = min(min(eta + 1, i), 4)
        w = (g * NEGINV16[(f & 15) // 2]) % (2**limit)
        g, q, r = g + w*f, q + w*u, r + w*v
    return eta, (u, v, q, r)

def modinv_var(M, Mi, x):
    """Compute the modular inverse of x mod M, given Mi = 1/M mod 2^N."""
    eta, f, g, d, e = -1, M, x, 0, 1
    while g != 0:
        eta, t = divsteps_n_matrix_var(eta, f % 2**N, g % 2**N)
        f, g = update_fg(f, g, t)
        d, e = update_de(d, e, t, M, Mi)
    return normalize(f, d, Mi)
```

## 8. From GCDs to Jacobi symbol

We can also use a similar approach to calculate Jacobi symbol _(x | M)_ by keeping track of an
extra variable _j_, for which at every step _(x | M) = j (g | f)_. As we update _f_ and _g_, we
make corresponding updates to _j_ using
[properties of the Jacobi symbol](https://en.wikipedia.org/wiki/Jacobi_symbol#Properties):

- _((g/2) | f)_ is either _(g | f)_ or _-(g | f)_, depending on the value of _f mod 8_ (negating if it's _3_ or _5_).
- _(f | g)_ is either _(g | f)_ or _-(g | f)_, depending on _f mod 4_ and _g mod 4_ (negating if both are _3_).

These updates depend only on the values of _f_ and _g_ modulo _4_ or _8_, and can thus be applied
very quickly, as long as we keep track of a few additional bits of _f_ and _g_. Overall, this
calculation is slightly simpler than the one for the modular inverse because we no longer need to
keep track of _d_ and _e_.

However, one difficulty of this approach is that the Jacobi symbol _(a | n)_ is only defined for
positive odd integers _n_, whereas in the original safegcd algorithm, _f, g_ can take negative
values. We resolve this by using the following modified steps:

```python
        # Before
        if delta > 0 and g & 1:
            delta, f, g = 1 - delta, g, (g - f) // 2

        # After
        if delta > 0 and g & 1:
            delta, f, g = 1 - delta, g, (g + f) // 2
```

The algorithm is still correct, since the changed divstep, called a "posdivstep" (see section 8.4
and E.5 in the paper) preserves _gcd(f, g)_. However, there's no proof that the modified algorithm
will converge. The justification for posdivsteps is completely empirical: in practice, it appears
that the vast majority of nonzero inputs converge to _f=g=gcd(f<sub>0</sub>, g<sub>0</sub>)_ in a
number of steps proportional to their logarithm.

Note that:

- We require inputs to satisfy _gcd(x, M) = 1_, as otherwise _f=1_ is not reached.
- We require inputs _x &neq; 0_, because applying posdivstep with _g=0_ has no effect.
- We need to update the termination condition from _g=0_ to _f=1_.

We account for the possibility of nonconvergence by only performing a bounded number of
posdivsteps, and then falling back to square-root based Jacobi calculation if a solution has not
yet been found.

The optimizations in sections 3-7 above are described in the context of the original divsteps, but
in the C implementation we also adapt most of them (not including "avoiding modulus operations",
since it's not necessary to track _d, e_, and "constant-time operation", since we never calculate
Jacobi symbols for secret data) to the posdivsteps version.
