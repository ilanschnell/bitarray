from socket import inet_aton, inet_ntoa

from bitarray.bitfields import compile


IPv4Header = compile("""
    # IPv4 header format
   >u4{version}               # IP version
    u4{header_length}         # header length in 32-bit words
    u6{dscp}                  # differentiated services code point
    u2{ecn}                   # explicit congestion notification
    u16{total_length}         # total packet length in bytes
    u16{identification}       # fragmentation identifier
    u3{flags}                 # fragmentation control flags
    u13{fragment_offset}      # fragment offset in 8-byte units
    u8{ttl}                   # time to live
    u8{protocol}              # transport protocol
    u16{checksum}             # header checksum
    B32{source_address}       # source IPv4 address
    B32{destination_address}  # destination IPv4 address
""")

values = (
    4,                          # version
    5,                          # header length
    0, 0,                       # DSCP, ECN
    20,                         # total length
    0x1234,                     # identification
    2, 0,                       # flags, fragment offset
    64, 17,                     # TTL, protocol (UDP)
    0,                          # checksum
    inet_aton("192.0.2.1"),     # source address
    inet_aton("198.51.100.2"),  # destination address
)

header = IPv4Header.pack(*values)
assert len(header) == 160

fields = IPv4Header.unpack(header)
print(inet_ntoa(fields.source_address))      # type: ignore
print(inet_ntoa(fields.destination_address)) # type: ignore
