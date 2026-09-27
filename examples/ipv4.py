from socket import inet_aton, inet_ntoa

from bitarray.bitfields import compile


IPv4Header = compile("""
    # IPv4 header format
    >u4    # version
    u4     # header length
    u6     # DSCP
    u2     # ECN
    u16    # total length
    u16    # identification
    u3     # flags
    u13    # fragment offset
    u8     # TTL
    u8     # protocol
    u16    # checksum
    B32    # source address
    B32    # destination address
""")

values = (
    4,                      # version
    5,                      # header length
    0, 0,                   # DSCP, ECN
    20,                     # total length
    0x1234,                 # identification
    2, 0,                   # flags, fragment offset
    64, 17,                 # TTL, protocol (UDP)
    0,                      # checksum
    inet_aton("192.0.2.1"),
    inet_aton("198.51.100.2"),
)

header = IPv4Header.pack(*values)
assert len(header) == 160

fields = IPv4Header.unpack(header)
print(inet_ntoa(fields[-2]))
print(inet_ntoa(fields[-1]))
