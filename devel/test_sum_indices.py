from bitarray.util import ones, sum_indices


N30 = 1 << 30  # 1 Gbit = 128 Mbyte
N32 = 1 << 32  # 4 Gbit = 512 Mbyte
N33 = 1 << 33  # 8 Gbit =   1 GByte

# For ones(n) with n > 2**32, the accumulated sum is flushed into a Python
# number object.
for n in [N30, N32, N32 + 1, 8 * N30, 16 * N30]:
    a = ones(n)
    print("n =    %25d  %6.2f Gbit    %6.2f GByte" % (n, n / N30, n / N33))
    print("2^63 = %25d" % (1 << 63))
    res = sum_indices(a)
    print("sum =  %25d" % res)
    assert res == n * (n - 1) // 2;
    print()

print("OK")
