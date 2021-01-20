def hww(tmp):
    tmp = 0xc147c1791d462d53e2c209d737faddc7
    print((bin(tmp).count('1')))
    print(hex(bin(tmp).count('1')))
    tmp -= (tmp >> 1) & bitv_m1
    print(hex(tmp))
    tmp = (tmp & bitv_m2) + ((tmp >> 2) & bitv_m2)
    print(hex(tmp))
    tmp = (tmp & bitv_m4) + ((tmp >> 4) & bitv_m4)
    print(hex(tmp))
    tmp = (tmp & bitv_m8) + ((tmp >> 8) & bitv_m8)
    print(hex(tmp))
    print(hex(((tmp * bitv_h01) >> 120)))


# tmp = (tmp + (tmp >> 4)) & bitv_m4
