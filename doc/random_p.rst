Implementation of ``util.random_p()``
=====================================

Bitarray 3.5 introduced the utility function ``util.random_p(n, p=0.5)``.
It returns a pseudo-random bitarray (of length ``n``) for which each bit has
probability ``p`` of being one.  This is mathematically equivalent to:

.. code-block:: python

    bitarray(random.random() < p for _ in range(n))

While this expression work well for small ``n``, it is quite slow when ``n``
is large.  In the following we focus on the case of ``n`` being large.

When ``p`` is small, a fast implementation of ``random_p()`` is to first
calculating the population of the bitarray, and then randomly set the
required number of bits.  Python 3.12 introduced ``random.binomialvariate()``
which is exactly what we need to to determine the bitarray's population.

When ``p == 0.5``, we use ``random.randbytes()`` initialize our bitarray
buffer.  Taking two (independent) such bitarrays and combining them
using the bitwise "and" operation, gives us a random bitarray with
probability 1/4.
Likewise, taking two bitwise "or" operation gives us probability 3/4.
Without going into too much further detail, it is possible to combine
more than two "randbytes" bitarray to get probabilities ``i / (1 << M)``,
where ``M`` is the maximal number of "randbytes" bitarrays we combine,
and ``i`` is an integer.
The required sequence of "or" and "and" operations is calculated from
the desired probability ``p`` and ``M``.
In our implementation, we choose ``M=8``.

Once we have calculated our sequence, and obtained a bitarray with
probability ``q = i / 256``, we perform a final "or" operation with
a random bitarray of probability ``x``.
In order to arrive at exactly the requested probability ``p``, it can
be verified that:

.. code-block:: python

    x = (p - q) / (1.0 - q)

It should also be noted that ``x`` is always small such that we can always
use the "small p case" which, unlike the combinations, gives us an bitarray
with exact probabilities.
