Dubner's conjecture
===================

Harvey Dubner proposed a strengthening of the Goldbach conjecture:
every even integer greater than 4208 is the sum of two twin primes (not
necessarily belonging to the same pair).
Only 34 even integers less than 4208 are not the sum of two twin primes.
Dubner has verified computationally that this list is complete up to 2e10.
A proof of this stronger conjecture would imply not only Goldbach's
conjecture but also the twin prime conjecture.  For more details,
see `Dubner's conjecture <https://oeis.org/A007534/a007534.pdf>`__.

In this document, we want to show how bitarrays can be used to calculate
twin primes and "middle numbers" very efficiently, and with very little
code.  We start by calculating all primes up to a limit ``N`` using
the `Sieve of Eratosthenes <../examples/sieve.py>`__:

.. code-block:: python

    >>> from bitarray.util import zeros, gen_primes
    >>> N = 1_000_000
    >>> primes = gen_primes(N)
    >>> list(primes.search(1, 0, 50))
    [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47]

In order to save memory and compute time, we only consider odd numbers
being twin primes.  Note that 2 is not considered a twin prime.

.. code-block:: python

    >>> twins = primes[1::2]
    >>> # we have a twin when next odd number is also prime:
    >>> twins &= twins << 1
    >>> # The first twin primes (only lower of each pair) are:
    >>> [2 * i + 1 for i in twins.search(1, 0, 60)]
    [3, 5, 11, 17, 29, 41, 59, 71, 101, 107]

We define a "middle number" to be ``m``, the number sandwiched between
a pair of twin primes ``m − 1`` and ``m + 1``.  It is obvious from the
characteristics of twin primes that all such ``m`` greater that 4 are
divisible by 6.  Again, to save memory and compute time, we only
consider multiples of 6.  If we let ``i=m//6``, we are looking for the
numbers ``i`` such that ``6*i-1``, ``6*i+1`` are twin primes.  The
numbers ``i`` are given in `sequence A002822 <https://oeis.org/A002822>`__.

.. code-block:: python

    >>> middles = zeros(1)  # middle numbers
    >>> middles += twins[2::3]
    >>> list(middles.search(1, 0, 46))  # sequence A002822
    [1, 2, 3, 5, 7, 10, 12, 17, 18, 23, 25, 30, 32, 33, 38, 40, 45]

Although not as memory efficient, a very elegant alternative to calculate
the middle numbers directly from primes is:

.. code-block:: python

    >>> ((primes >> 1) & (primes << 1))[::6] == middles
    True

We now mark multiples of 6 that are sum of two middle numbers:

.. code-block:: python

    >>> M = len(middles)
    >>> mark = zeros(M)
    >>> for i in middles.search(1):
    ...     mark[i:] |= middles[:M - i]

Positive integers divisible by 6 and greater than 6 that are not the sum
of two middle numbers (greater than 4):

.. code-block:: python

    >>> [6 * i for i in mark.search(0, 2)]
    [96, 402, 516, 786, 906, 1116, 1146, 1266, 1356, 3246, 4206]

This is `sequence A179825 <https://oeis.org/A179825>`__, the multiples of 6
which are not the sum of a pair of twin primes.
None of the above values are middle numbers themselves (this would
contradict Conjecture 1):

.. code-block:: python

    >>> any(middles[m] for m in mark.search(0, 2))
    False

As `A007534 <https://oeis.org/A007534>`__, is the sequence of positive even
numbers that are not the sum of a pair of twin primes (not just multiples
of 6), A179825 is a subset of A007534.

.. image:: https://github.com/ilanschnell/visual/blob/master/dubner/image.png?raw=true
   :alt: visualization of middle numbers
   :width: 1290px
   :height: 680px
