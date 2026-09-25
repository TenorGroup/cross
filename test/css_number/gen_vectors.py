#!/usr/bin/env python3
"""Cases for the CSS float parser, each with the float std::from_chars must give (libstdc++): the
nearest float to the exact decimal value, ties to even; an error past the largest float or for a
nonzero value that rounds to zero. Exact rational arithmetic, so the answers do not come from any
C library. Deterministic: the same seed writes the same file."""
from fractions import Fraction
import random
import struct
import sys

MIN_SUB = Fraction(1, 2**149)


def to_bits(x):
    """(ok, bits) for the positive Fraction x rounded to float32."""
    if x == 0:
        return True, 0
    e = x.numerator.bit_length() - x.denominator.bit_length()
    if Fraction(2) ** e > x:
        e -= 1
    ulp = MIN_SUB if e < -126 else Fraction(2) ** (e - 23)
    q = x / ulp
    n = q.numerator // q.denominator
    r = q - n
    if r > Fraction(1, 2) or (r == Fraction(1, 2) and n % 2 == 1):
        n += 1
    value = n * ulp
    if value == 0 or value >= Fraction(2) ** 128:
        return False, 0
    return True, struct.unpack('<I', struct.pack('<f', float(value)))[0]


def exact(text):
    neg = text.startswith('-')
    body = text[1:] if neg else text
    if '.' in body:
        i, f = body.split('.')
    else:
        i, f = body, ''
    return neg, Fraction(int((i + f) or '0'), 10 ** len(f))


def decimal(x, digits=None):
    """The exact decimal expansion of a dyadic Fraction, or its first `digits` significant digits."""
    p, q = x.numerator, x.denominator
    k = q.bit_length() - 1  # q is a power of two
    s = str(p * 5 ** k).rjust(k + 1, '0')
    text = (s[:-k] or '0') + ('.' + s[-k:] if k else '')
    return text


def case(text, out):
    neg, x = exact(text)
    ok, bits = to_bits(x)
    if ok and neg:
        bits |= 0x80000000
    out.append((text, ok, bits))


def main():
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    rnd = random.Random(seed)
    out = []
    for t in ['0', '1', '1.5', '0.83', '1.17', '.5', '-0.25', '12', '100', '0.75', '2.5', '33.333333', '66.6667',
              '0.0625', '5.', '-.5', '-0', '0.000', '00012.500', '3.14159265358979323846', '0.1', '0.2', '0.3',
              '16777216', '16777217', '16777218', '16777219', '33554433', '0.000001', '1234567.8']:
        case(t, out)
    floats = []
    for _ in range(3000):
        bits = rnd.randrange(1, 0x7F7FFFFF)
        floats.append(bits)
    floats += [1, 2, 3, 0x007FFFFF, 0x00800000, 0x00800001, 0x7F7FFFFE, 0x7F7FFFFF, 0x3F800000, 0x3F7FFFFF]
    for bits in floats:
        v = Fraction(struct.unpack('<f', struct.pack('<I', bits))[0])
        nxt = Fraction(struct.unpack('<f', struct.pack('<I', bits + 1))[0]) if bits < 0x7F7FFFFF else \
            Fraction(2) ** 128
        mid = (v + nxt) / 2
        m = decimal(mid)
        case(m, out)  # exactly on the midpoint: ties to even
        # Just either side: the last digit one up, and the value cut short.
        up = m + '1' if '.' in m else m + '.1'
        case(up, out)
        head = m[:40] if len(m) > 40 else m
        case(head, out)
        case(decimal(v)[:60] if len(decimal(v)) > 60 else decimal(v), out)
    for _ in range(12000):
        ip = str(rnd.randrange(0, 10 ** rnd.randrange(1, 10)))
        fp = ''.join(rnd.choice('0123456789') for _ in range(rnd.randrange(0, 13)))
        t = ('-' if rnd.random() < 0.2 else '') + ('0' * rnd.randrange(0, 3)) + ip + ('.' + fp if fp or rnd.random() < 0.1 else '')
        case(t, out)
    for _ in range(1500):
        # Tiny and huge values, and long strings past the 120 kept digits.
        t = '0.' + '0' * rnd.randrange(35, 50) + str(rnd.randrange(1, 10 ** rnd.randrange(1, 20)))
        case(t, out)
        t = str(rnd.randrange(1, 10)) + ''.join(rnd.choice('0123456789') for _ in range(rnd.randrange(35, 41)))
        case(t, out)
        t = '1.' + ''.join(rnd.choice('0123456789') for _ in range(rnd.randrange(100, 200)))
        case(t, out)
    with open(sys.argv[1], 'w') as f:
        for text, ok, bits in out:
            f.write('{"%s", %s, 0x%08Xu},\n' % (text, 'true' if ok else 'false', bits))


if __name__ == '__main__':
    main()
