# Copyright (c) 2026, Ilan Schnell; All Rights Reserved
# bitarray is published under the PSF license.
#
# Author: Ilan Schnell
"""
Pack and unpack bit-level structures using `bitarray`.

A format is a sequence of fields; whitespace between fields is optional.
Compile the format once, then use the resulting `Struct` instance to pack
values into a bitarray or unpack a bitarray into a tuple::

    from bitarray.bitfields import compile

    cf = compile("u3 s5 ? x2 h12 B16 f32")
    values = (5, -3, False, "1fa", b"AB", 1.5)
    a = cf.pack(*values)
    assert cf.unpack(a) == values

Padding fields do not consume or produce values.  The input to `unpack()`
must have exactly `cf.width` bits.  `cf.values` gives the number of
values consumed by `pack()` and returned by `unpack()`.

The supported field codes are:

u   An unsigned integer stored in the field width.
s   A signed integer stored in the field width using two's-complement
    representation.
?   A bool stored in field width one.  Packing uses normal Python truth-value
    testing.  Unpacking returns `bool`.
f   An IEEE floating-point value.  The width must be 16, 32, or 64.
h   A hexadecimal string occupying the field width.  The width must be a
    multiple of four; packing is case-insensitive and ignores whitespace;
    unpacking returns lowercase.
b   A bitarray matching the field width.
B   A bytes or bytearray value occupying the field width.  The width must be
    a multiple of eight; unpacking always returns `bytes`.
x   Zero-padding bits.
X   One-padding bits.

The width *N* defaults to one when omitted.  Each field may be prefixed
with `<` for little-endian or `>` for big-endian bit and byte order.  The
selected order applies to subsequent fields until changed; initially it is
little-endian.
"""
import re
import struct
import functools
from dataclasses import dataclass
from typing import Any, Tuple

from bitarray import bitarray
from bitarray.util import int2ba, ba2int, hex2ba, ba2hex


__all__ = ["Struct", "compile", "pack", "unpack"]


DEFAULT_ENDIAN = "little"

_ENDIAN_FROM_PREFIX = {"<": "little", ">": "big"}
_PREFIX_FROM_ENDIAN = {v: k for k, v in _ENDIAN_FROM_PREFIX.items()}


@dataclass(frozen=True)
class _Field:

    width: int
    endian: str
    consumes_value = True
    code = ""

    def format(self):
        return "%s%s%d" % (_PREFIX_FROM_ENDIAN[self.endian],
                           self.code, self.width)


@dataclass(frozen=True)
class _IntField(_Field):

    signed: bool

    @property
    def code(self):
        return "s" if self.signed else "u"

    def pack(self, value):
        return int2ba(value, length=self.width, endian=self.endian,
                      signed=self.signed)

    def unpack(self, a):
        return ba2int(a, signed=self.signed)


@dataclass(frozen=True)
class _BoolField(_Field):

    code = "?"

    def __post_init__(self):
        if self.width != 1:
            raise ValueError("bool width must be 1")

    def pack(self, value):
        return bitarray("1" if value else "0")

    def unpack(self, a):
        return bool(a[0])


@dataclass(frozen=True)
class _FloatField(_Field):

    code = "f"
    formats = {16: "e", 32: "f", 64: "d"}

    def __post_init__(self):
        if self.width not in self.formats:
            raise ValueError("float must have width 16, 32 or 64, got %d" %
                             self.width)

    def struct_format(self):
        return _PREFIX_FROM_ENDIAN[self.endian] + self.formats[self.width]

    def pack(self, value):
        return bitarray(struct.pack(self.struct_format(), value),
                        endian=self.endian)

    def unpack(self, a):
        return struct.unpack(self.struct_format(), bytes(a))[0]


@dataclass(frozen=True)
class _HexField(_Field):

    code = "h"

    def __post_init__(self):
        if self.width % 4:
            raise ValueError("width not a multiple of 4")

    def pack(self, value):
        if not isinstance(value, str):
            raise TypeError("str expected, got %r" % type(value).__name__)
        a = hex2ba(value, self.endian)
        if len(a) != self.width:
            raise ValueError("hex string with %d digits expected" %
                             (self.width // 4))
        return a

    def unpack(self, a):
        return ba2hex(a)


@dataclass(frozen=True)
class _BitarrayField(_Field):

    code = "b"

    def pack(self, a):
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width:
            raise ValueError("bitarray of length %d expected" % self.width)
        return a

    def unpack(self, a):
        return a


@dataclass(frozen=True)
class _BytesField(_Field):

    code = "B"

    def __post_init__(self):
        if self.width % 8:
            raise ValueError("width not a multiple of 8")

    def pack(self, value):
        if not isinstance(value, (bytes, bytearray)):
            raise TypeError("bytes expected, got %r" % type(value).__name__)
        if len(value) != self.width // 8:
            raise ValueError("bytes of length %d expected" %
                             (self.width // 8))
        return bitarray(value, endian=self.endian)

    def unpack(self, a):
        return bytes(a)


@dataclass(frozen=True)
class _PaddingField(_Field):

    value: bool
    consumes_value = False

    @property
    def code(self):
        return "X" if self.value else "x"

    def pack(self, value):
        return self.width * bitarray("1" if self.value else "0")

    def unpack(self, a):
        pass


@dataclass(frozen=True)
class Struct:
    "Struct(format) -> compiled struct object"

    _fields: tuple
    width: int
    values: int

    def __init__(self, format: str = "") -> None:
        fields = self._fields_from_format(format)
        object.__setattr__(self, "_fields", tuple(fields))
        object.__setattr__(self, "width", sum(f.width for f in fields))
        object.__setattr__(self, "values", sum(f.consumes_value
                                               for f in fields))

    _pat = re.compile(r"""
        ([<>])?   # optional prefix; < or >
        ([\w?])   # code character
        (\d*)     # optional bit width; defaults to 1
        \s*       # optional whitespace
    """, re.VERBOSE)

    def _fields_from_format(self, format):
        endian = DEFAULT_ENDIAN
        fields = []
        format = format.lstrip()
        while format:
            m = self._pat.match(format)
            if m is None:
                raise ValueError("invalid format: %r" % format)
            pre = m.group(1)
            if pre is not None:
                endian = _ENDIAN_FROM_PREFIX[pre]
            c = m.group(2)
            w = int(m.group(3) or 1)
            if w == 0:
                raise ValueError("field width cannot be zero: %r" % format)
            fields.append(self._field_from_code(c, w, endian))
            format = format[m.end():]

        return fields

    @staticmethod
    def _field_from_code(code, width, endian):
        if code in "us":
            return _IntField(width, endian, signed=(code == "s"))
        if code == "?":
            return _BoolField(width, endian)
        if code == "f":
            return _FloatField(width, endian)
        if code == "h":
            return _HexField(width, endian)
        if code == "b":
            return _BitarrayField(width, endian)
        if code == "B":
            return _BytesField(width, endian)
        if code in "xX":
            return _PaddingField(width, endian, value=(code == "X"))
        raise ValueError("Not a valid code: %r" % code)

    def format(self) -> str:
        """format() -> str

Return the canonical format string reconstructed from this compiled format.
"""
        return " ".join(field.format() for field in self._fields)

    def __repr__(self):
        return "Struct(%r)" % self.format()

    def pack(self, *values: Any) -> bitarray:
        """pack(v1, v2, ...) -> bitarray

Return a bitarray containing the values v1, v2, ... packed according to this
compiled format.
"""
        if len(values) != self.values:
            raise ValueError("expected %d values to pack, got %d" %
                             (self.values, len(values)))
        fields = self._fields
        a = bitarray(0, fields[0].endian if fields else DEFAULT_ENDIAN)
        i = 0  # value index
        for field in fields:
            value = None
            if field.consumes_value:
                value = values[i]
                i += 1
            a.extend(field.pack(value))
        return a

    def unpack(self, a: bitarray) -> Tuple[Any, ...]:
        """unpack(bitarray) -> tuple

Return a tuple containing values unpacked according to this compiled format.
"""
        if not isinstance(a, bitarray):
            raise TypeError("bitarray expected, got %r" % type(a).__name__)
        if len(a) != self.width:
            raise ValueError("expected bitarray of length %d, got %d" %
                             (self.width, len(a)))
        i = 0
        res = []
        for field in self._fields:
            j = i + field.width
            b = bitarray(a[i:j], field.endian)
            value = field.unpack(b)
            if field.consumes_value:
                res.append(value)
            i = j
        return tuple(res)


@functools.lru_cache()
def compile(format: str) -> Struct:
    """compile(format) -> Struct

Compile given format string and return a compiled format object that
can be used to pack and/or unpack data multiple times.
"""
    return Struct(format)

def pack(format: str, *values: Any) -> bitarray:
    """pack(format, v1, v2, ...) -> bitarray

Return a bitarray containing the values v1, v2, ... packed according
to the format string.
"""
    cf = compile(format)
    return cf.pack(*values)

def unpack(format: str, a: bitarray) -> Tuple[Any, ...]:
    """unpack(format, bitarray) -> tuple

Return a tuple containing values unpacked according to the format string.
"""
    if not isinstance(a, bitarray):
        raise TypeError("bitarray expected, got %r" % type(a).__name__)
    cf = compile(format)
    return cf.unpack(a)
