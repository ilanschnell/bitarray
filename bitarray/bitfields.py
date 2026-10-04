# Copyright (c) 2026, Ilan Schnell; All Rights Reserved
# bitarray is published under the PSF license.
#
# Author: Ilan Schnell
"""
Pack and unpack bit-level structures using `bitarray`.

https://github.com/ilanschnell/bitarray/blob/master/doc/bitfields.rst
"""
import re
import struct
import functools
import collections
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
    name: str
    has_value = True
    code = ""

    @property
    def prefix(self):
        return _PREFIX_FROM_ENDIAN[self.endian]

    def format(self):
        res = "%s%s%s" % (self.prefix, self.code, self.width)
        if self.name:
            res += "{%s}" % self.name
        return res


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
            raise ValueError("bool field width must be 1, "
                             "got %d" % self.width)

    def pack(self, value):
        return bitarray("1" if value else "0")

    def unpack(self, a):
        return bool(a[0])

    def format(self):
        res = "%s?" % self.prefix
        if self.name:
            res += "{%s}" % self.name
        return res


@dataclass(frozen=True)
class _FloatField(_Field):

    code = "f"
    formats = {16: "e", 32: "f", 64: "d"}

    def __post_init__(self):
        if self.width not in self.formats:
            raise ValueError("float field width must be 16, 32 or 64, "
                             "got %d" % self.width)

    def struct_format(self):
        return self.prefix + self.formats[self.width]

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
            raise ValueError("hexadecimal field width must be a "
                             "multiple of 4, got %d" % self.width)

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
            raise ValueError("bytes field width must be a "
                             "multiple of 8, got %d" % self.width)

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
    # For padding fields, the inherited name attribute stores the canonical
    # explicit bit pattern, or "" when the field uses repeated padding.

    value: bool
    validate: bool
    has_value = False

    @classmethod
    def from_code(cls, code, width, endian, name):
        if name:
            a = bitarray(name)  # validates and ignores underscores
            if width != len(a):
                raise ValueError("pad-bits width mismatch: %d != %d" %
                                 (width, len(a)))
            if a.count() in (0, width):
                value, name = bool(a[0]), ""
            else:
                value, name = False, a.to01()
        else:
            value = code.isupper()
        return cls(width, endian, name, value=value,
                   validate=(code.lower() == "p"))

    @property
    def code(self):
        return ("pP" if self.validate else "xX")[self.value]

    @property
    def bits(self):
        return bitarray(self.name or self.width * ("01"[self.value]))

    def pack(self, unused_value):
        return self.bits

    def unpack(self, a):
        if self.validate and a != self.bits:
            raise ValueError("pad-bits mismatch: %s != %s" %
                             (a.to01(), self.bits.to01()))


@dataclass(frozen=True)
class Struct:
    """Struct(format: str) -> compiled struct object

Central class for packing and unpacking bit-level structures.
"""

    _fields: tuple
    width: int
    values: int
    _names: tuple

    def __init__(self, format: str = "") -> None:
        fields = self._fields_from_format(format)
        object.__setattr__(self, "_fields", tuple(fields))
        object.__setattr__(self, "width", sum(f.width for f in fields))
        object.__setattr__(self, "values", sum(f.has_value for f in fields))
        object.__setattr__(self, "_names", self._get_names())

    def _get_names(self):
        names = tuple(f.name for f in self._fields if f.has_value)
        if not any(names):
            return ()
        if not all(names):
            raise ValueError("Some but not all fields have a name")
        self._result_type(names)  # validate and warm cache
        return names

    @staticmethod
    @functools.lru_cache()
    def _result_type(names):
        return collections.namedtuple("Unpacked", names)

    _pat = re.compile(r"""
    ([<>])?         # optional prefix
    ([a-zA-Z?])     # code character
    (\d+)?          # optional bit width
    (?:\{(\w+)\})?  # optional name (or padding bit pattern)
    \s*             # optional whitespace
    """, re.VERBOSE | re.ASCII)

    def _fields_from_format(self, format):
        endian = DEFAULT_ENDIAN
        fields = []
        format = re.sub(r"#[^\r\n]*", "", format)  # strip comments
        format = format.lstrip()
        while format:
            m = self._pat.match(format)
            if m is None:
                raise ValueError("invalid format: %r" % format)
            pre = m.group(1)
            if pre:
                endian = _ENDIAN_FROM_PREFIX[pre]
            code = m.group(2)
            name = m.group(4) or ""
            width = int(m.group(3) or self._default_width(code, name))
            if width == 0:
                raise ValueError("field width cannot be zero: %r" % format)
            fields.append(self._field_from_code(code, width, endian, name))
            format = format[m.end():]

        return fields

    @staticmethod
    def _default_width(code, name):
        if code in "pPxX" and name:
            # Underscores do not contribute to the width.
            return len(name) - name.count("_")
        return {"h": 4, "B": 8}.get(code, 1)

    @staticmethod
    def _field_from_code(code, width, endian, name):
        if code in "us":
            return _IntField(width, endian, name, signed=(code == "s"))
        if code == "?":
            return _BoolField(width, endian, name)
        if code == "f":
            return _FloatField(width, endian, name)
        if code == "h":
            return _HexField(width, endian, name)
        if code == "b":
            return _BitarrayField(width, endian, name)
        if code == "B":
            return _BytesField(width, endian, name)
        if code in "pPxX":
            return _PaddingField.from_code(code, width, endian, name)
        raise ValueError("invalid code: %r" % code)

    def format(self) -> str:
        """format() -> str

Return the canonical format string reconstructed from this compiled format.
"""
        return " ".join(field.format() for field in self._fields)

    def __repr__(self):
        return "Struct(%r)" % self.format()

    def __reduce__(self):
        return Struct, (self.format(), )

    def pack(self, *values: Any) -> bitarray:
        """pack(v1, v2, ...) -> bitarray

Return a bitarray containing the values v1, v2, ... packed according to this
compiled format.
"""
        if len(values) != self.values:
            raise ValueError("expected %d values to pack, got %d" %
                             (self.values, len(values)))
        value_iter = iter(values)
        fields = self._fields
        a = bitarray(0, fields[0].endian if fields else DEFAULT_ENDIAN)
        for field in fields:
            value = next(value_iter) if field.has_value else None
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
        values = []
        for field in self._fields:
            j = i + field.width
            # Copy logical bits into endianness required by this field.
            value = field.unpack(bitarray(a[i:j], field.endian))
            if field.has_value:
                values.append(value)
            i = j
        if self._names:
            return self._result_type(self._names)(*values)
        return tuple(values)


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
    return compile(format).pack(*values)

def unpack(format: str, a: bitarray) -> Tuple[Any, ...]:
    """unpack(format, bitarray) -> tuple

Return a tuple containing values unpacked according to the format string.
"""
    return compile(format).unpack(a)
