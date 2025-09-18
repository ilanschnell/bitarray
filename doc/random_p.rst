Random Bitarrays
================

Bitarray 3.5 introduced the utility function ``util.random_p(n, p=0.5)``.
It returns a pseudo-random bitarray (of length ``n``) for which each bit has
probability ``p`` of being one.  This is mathematically equivalent to:

.. code-block:: python

    bitarray(random() < p for _ in range(n))

While this expression work well for small ``n``, it is quite slow when ``n``
is large.  In the following we focus on the case of ``n`` being large.

When ``p`` is small, a fast implementation of ``random_p()`` is to (a)
calculate the population of the bitarray, and then (b) set the required
number of bits, using ``random.randrange()`` for each bit.
Python 3.12 introduced ``random.binomialvariate()`` which is exactly what we
need to determine the bitarray's population.

When ``p == 0.5``, we use ``random.getrandbits()`` to initialize our bitarray
buffer.  It should be noted that ``util.urandom()`` uses ``os.urandom()``,
but since ``util.random_p()`` is designed to give reproducible pseudo-random
bitarrays, it uses ``getrandbits()``.

Taking two (independent) such bitarrays and combining them
using the bitwise AND operation, gives us a random bitarray with
probability 1/4.
Likewise, taking two bitwise OR operation gives us probability 3/4.
Without going into too much further detail, it is possible to combine
more than two "getrandbits" bitarray to get probabilities ``i / 2**M``,
where ``M`` is the maximal number of "getrandbits" bitarrays we combine,
and ``i`` is an integer.
The required sequence of AND and OR operations is calculated from
the desired probability ``p`` and ``M``.

Once we have calculated our sequence, and obtained a bitarray with
probability ``q = i / 2**M``, we perform a final OR or AND operation with
a random bitarray of probability ``x``.
In order to arrive at exactly the requested probability ``p``, it can
be verified that:

.. code-block:: python

    x = (p - q) / (1.0 - q)  # OR
    x = p / q                # AND

It should be noted that ``x`` is always small (once symmetry is applied in
case of AND) such that it always uses the "small p" case.
Unlike the combinations, this gives us a bitarray
with exact probability ``x``.  Therefore, the requested probability ``p``
is exactly obtained.
For more details, see ``VerificationTests`` in the
additional `random tests <../devel/test_random.py>`__.


Speedup
-------

The speedup is largest, when the number of number of random numbers our
algorithm uses is smallest.
In the following, let ``k`` be the number of calls to ``getrandbits()``.
For example, when ``p=0.5`` we have ``k=1``.
When ``p`` is below our limit for using the procedure of setting individual
bits, we call this limit ``small_p``, we have ``k=0``.

In our implementation, we are using ``M=8`` and ``small_p=0.01``.
These parameters have carefully been selected to optimize the average (with
respect to ``p``) execution time.
The following table shows execution times (in milliseconds) of ``random_p()``
for different values of ``p`` for ``n=100_000_000``:

.. code-block::

      p          t/ms    k    notes
   -----------------------------------------------------------------------
   edge cases:
     0.0          0.4    0
     0.5         21.7    1
     1.0          0.4    0

   pure combinations:
     1/4         44.6    2
     1/8         65.2    3
     1/16        88.7    4
     1/32       108.6    5
     1/64       132.4    6
     3/128      151.9    7    p = 1/128 < small_p, so we take different p
   127/256      174.9    8    priciest pure combinations case(s)

   small p:
   0.0001         2.2    0
   0.001         18.7    0
   0.003891051   72.9    0    p = 1/257 - largest x in mixed case
   0.009999999  192.3    0    priciest small p case

   mixed:                     x  (final operation)
   0.01         194.3    7    0.002204724  OR   smallest p for mixed case
   0.1          223.4    8    0.002597403  OR
   0.2          194.7    8    0.000975610  OR
   0.3          213.7    8    0.997402597  AND
   0.4          203.3    7    0.002597403  OR
   0.252918288  118.7    2    0.003891051  OR   p=65/257
   0.494163425  249.5    8    0.996108951  AND  priciest mixed case(s)
   0.499999999   22.4    1    0.999999998  AND  cheapest mixed case

   literal:
   any         3740.2    -    bitarray(random() < p for _ in range(n))


Using the literal definition one always uses ``n`` calls to ``random()``,
regardless of ``p``.
For 1000 random values of ``p`` (between 0 and 1), we get an average speedup
of about 19.

In summary: Even in the worst cases ``random_p()`` performs about 15 times
better than the literal definition for large ``n``, while on average we get
a speedup of almost 20.  For very small ``p``, and for special values of ``p``
the speedup is significantly higher.
