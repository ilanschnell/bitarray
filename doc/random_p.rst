Random Bitarrays
================

Bitarray 3.5 introduced the utility function ``util.random_p(n, p=0.5)``.
It returns a pseudo-random bitarray (of length ``n``) for which each bit has
probability ``p`` of being one.  This is mathematically equivalent to:

.. code-block:: python

    bitarray(random() < p for _ in range(n))

While this expression work well for small ``n``, it is quite slow when ``n``
is large.  In the following we focus on the case of ``n`` being large.

When ``p`` is small, a fast implementation of ``random_p()`` is to first
calculating the population of the bitarray, and then randomly set the
required number of bits.  Python 3.12 introduced ``random.binomialvariate()``
which is exactly what we need to to determine the bitarray's population.

When ``p == 0.5``, we use ``random.randbytes()`` to initialize our bitarray
buffer.  It should be noted that ``util.urandom()`` uses ``os.urandom()``,
but since ``util.random_p()`` is designed to give reproducible pseudo-random
bitarray, it uses ``randbytes()``.

Taking two (independent) such bitarrays and combining them
using the bitwise "and" operation, gives us a random bitarray with
probability 1/4.
Likewise, taking two bitwise "or" operation gives us probability 3/4.
Without going into too much further detail, it is possible to combine
more than two "randbytes" bitarray to get probabilities ``i / (1 << M)``,
where ``M`` is the maximal number of "randbytes" bitarrays we combine,
and ``i`` is an integer.
The required sequence of "or" and "and" operations is calculated from
the desired probability ``p`` and ``M``.

Once we have calculated our sequence, and obtained a bitarray with
probability ``q = i / 2**M``, we perform a final "or" operation with
a random bitarray of probability ``x``.
In order to arrive at exactly the requested probability ``p``, it can
be verified that:

.. code-block:: python

    x = (p - q) / (1.0 - q)

It should also be noted that ``x`` is always small such that we can always
use the "small p" case which, unlike the combinations, gives us an bitarray
with exact probabilities.


Speedup
-------

The speedup is largest, when the number of number of random numbers our
algorithm uses is smallest.
In the following, let ``k`` be the number of calls to ``randbytes()``.
For example, when ``p=0.5`` we have ``k=1``.
When ``p`` is below our limit for using the procedure of setting individual
bits, we call this limit ``small_p``, we have ``k=0``.

In our implementation, we are using ``M=8`` and value of ``small_p=0.01``.
The following table shows execution times (in milliseconds) for different
values of ``p`` for ``n=100_000_000``:

.. code-block::

      p        t/ms    k    notes
   -----------------------------------------------------------------------
   edge cases:
     0.0        0.4    0
     0.5       21.7    1
     1.0        0.4    0

   pure combinations:
     1/4       44.6    2
     1/8       65.2    3
     1/16      88.7    4
     1/32     108.6    5
     1/64     132.4    6
    63/128    151.9    7    p = 1/128 < small_p, so we take different p
   127/256    174.9    8    priciest pure combinations cases

   small p:
   0.009999   192.3    0    priciest small p case
   0.007752   142.3    0    p=1/129 - highest x from mixed case
   0.001       18.7    0
   0.0001       2.2    0

   mixed:                     x
   0.01       194.3    7    0.00220472  smallest p for mixed case
   0.1        223.4    8    0.00259740
   0.2        194.7    8    0.00097561
   0.3        213.7    6    0.00444444
   0.4        203.3    7    0.00259740
   0.4999999  316.8    8    0.00775192  priciest case overall (x=1/129)

   literal:
   any       3690.2    -    bitarray(random() < p for _ in range(n))


Using the literal definition one always uses ``n`` calls to ``randrange()``,
regardless of ``p``.
For 1000 random values of ``p`` (between 0 and 1), we get an average speedup
of about 19.

In summary: Even in the worst case ``random_p()`` performs more than 10 times
better than the literal definition for large ``n``, while on average we get
a speedup up about 19.  For very small ``p``, and for special values of ``p``
the speedup is much bigger.
