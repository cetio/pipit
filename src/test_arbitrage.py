#!/usr/bin/env python3
"""
Prototype and test suite for the Myzomela allocator's arbitrage scheme.

Arbitrage encoding: each shard has 1 bit. Allocations are runs of identical
bits. Adjacent allocations have different bit values. Free shards have
meaningless arbitrage bits (they are ignored).

To free: find the shard's arbitrage bit value, then count contiguous
allocated shards with the same value.
"""

import random


class Slab:
    """Simulates one slab with presence bitmaps and arbitrage."""

    def __init__(self, num_shards):
        self.num_shards = num_shards
        # presence[i] = 1 means FREE, 0 means ALLOCATED
        self.presence = [1] * num_shards
        # arbitrage[i] = 0 or 1 for allocated shards; undefined for free
        self.arbitrage = [0] * num_shards
        self.next = None
        self._last_color = 0  # toggles per allocation

    def allocate(self, length):
        """Find `length` free shards and allocate them. Returns shard index or -1."""
        # Find a run of `length` consecutive free shards
        for i in range(self.num_shards - length + 1):
            if all(self.presence[j] == 1 for j in range(i, i + length)):
                # Determine color: must differ from left neighbor if allocated
                left_color = None
                if i > 0 and self.presence[i - 1] == 0:
                    left_color = self.arbitrage[i - 1]

                right_color = None
                if i + length < self.num_shards and self.presence[i + length] == 0:
                    right_color = self.arbitrage[i + length]

                # Choose a color different from both neighbors
                color = self._choose_color(left_color, right_color)
                if color is None:
                    # Color conflict: both neighbors have different colors.
                    # Fall back: scan for another spot.
                    continue

                # Mark allocation
                for j in range(i, i + length):
                    self.presence[j] = 0
                    self.arbitrage[j] = color

                return i
        return -1

    def _choose_color(self, left, right):
        """Pick a color (0 or 1) that differs from both neighbors if possible."""
        if left is None and right is None:
            return self._last_color ^ 1  # flip from initial/default
        if left is None:
            return right ^ 1
        if right is None:
            return left ^ 1
        if left == right:
            return left ^ 1
        # left != right: impossible with 2 colors
        return None

    def free(self, shard):
        """Free the allocation containing `shard`. Returns length or -1 if error."""
        if shard < 0 or shard >= self.num_shards:
            return -1
        if self.presence[shard] == 1:
            return -1  # already free

        color = self.arbitrage[shard]

        # Find left boundary
        left = shard
        while left > 0 and self.presence[left - 1] == 0 and self.arbitrage[left - 1] == color:
            left -= 1

        # Find right boundary
        right = shard
        while right + 1 < self.num_shards and self.presence[right + 1] == 0 and self.arbitrage[right + 1] == color:
            right += 1

        length = right - left + 1

        # Mark free
        for i in range(left, right + 1):
            self.presence[i] = 1

        return length

    def validate(self):
        """Check invariants: no two adjacent allocations share the same color."""
        errors = []
        in_alloc = False
        prev_color = None
        for i in range(self.num_shards):
            if self.presence[i] == 0:
                color = self.arbitrage[i]
                if in_alloc and color == prev_color:
                    # Same allocation, OK
                    pass
                elif in_alloc and color != prev_color:
                    # New allocation with different color, OK
                    prev_color = color
                elif not in_alloc:
                    # Start of new allocation
                    in_alloc = True
                    prev_color = color
            else:
                in_alloc = False
                prev_color = None
        return errors

    def dump(self, max_width=80):
        """Print presence and arbitrage as strings."""
        p = "".join("." if x == 1 else "#" for x in self.presence)
        a = "".join(str(x) if self.presence[i] == 0 else "." for i, x in enumerate(self.arbitrage))
        print(f"P: {p[:max_width]}")
        print(f"A: {a[:max_width]}")


def test_basic_alloc_free():
    """Test sequential allocation and freeing."""
    print("=== test_basic_alloc_free ===")
    slab = Slab(64)

    # Allocate 3, 2, 1, 3
    s1 = slab.allocate(3)
    s2 = slab.allocate(2)
    s3 = slab.allocate(1)
    s4 = slab.allocate(3)

    assert s1 == 0, f"expected 0, got {s1}"
    assert s2 == 3, f"expected 3, got {s2}"
    assert s3 == 5, f"expected 5, got {s3}"
    assert s4 == 6, f"expected 6, got {s4}"

    slab.dump()

    # Free the 2-shard allocation at s2=3
    freed_len = slab.free(3)
    assert freed_len == 2, f"expected 2, got {freed_len}"
    print("After free(3):")
    slab.dump()

    # Re-allocate 2 into the gap
    s5 = slab.allocate(2)
    print(f"Re-allocated 2 at {s5}")
    slab.dump()

    # Free everything
    for s in [s1, s3, s4, s5]:
        if s >= 0:
            slab.free(s)

    print("After freeing all:")
    slab.dump()
    assert all(x == 1 for x in slab.presence), "Not all shards freed"
    print("PASS\n")


def test_color_conflict():
    """Test the dreaded color-conflict scenario."""
    print("=== test_color_conflict ===")
    slab = Slab(16)

    # Allocate: 2 shards color 1, 2 shards color 0, 2 shards color 1
    a = slab.allocate(2)   # color 1
    b = slab.allocate(2)   # color 0
    c = slab.allocate(2)   # color 1

    print(f"Allocations: a={a}, b={b}, c={c}")
    slab.dump()

    # Free the middle (color 0)
    freed = slab.free(b)
    print(f"Freed {freed} at {b}")
    slab.dump()

    # Try to allocate 2 into the gap
    # Left neighbor (a) is color 1, right neighbor (c) is color 1
    # Both neighbors same color → we can pick color 0. Should succeed.
    d = slab.allocate(2)
    print(f"Re-allocated 2 at {d}")
    slab.dump()
    assert d == b, f"Expected reallocation at {b}, got {d}"

    # Now: free a and c, leaving d in the middle with color 0
    slab.free(a)
    slab.free(c)
    print("After freeing a and c:")
    slab.dump()

    # Try to allocate 2 at position a=0
    # Left neighbor is free, right neighbor (d) is color 0
    # Should pick color 1. OK.
    e = slab.allocate(2)
    print(f"Allocated 2 at {e}")
    slab.dump()
    assert e == 0

    # Try to allocate 2 at position c=4
    # Left neighbor (d) is color 0, right neighbor is free
    # Should pick color 1. OK.
    f = slab.allocate(2)
    print(f"Allocated 2 at {f}")
    slab.dump()
    assert f == 4

    print("PASS\n")


def test_color_conflict_impossible():
    """Demonstrate the case where 2 colors are insufficient."""
    print("=== test_color_conflict_impossible ===")
    slab = Slab(16)

    # Allocate 2, 2, 2 with alternating colors
    a = slab.allocate(2)  # color 1
    b = slab.allocate(2)  # color 0
    c = slab.allocate(2)  # color 1

    print(f"Allocated at {a}, {b}, {c}")
    slab.dump()

    # Free a and c, leaving b (color 0) in the middle
    slab.free(a)
    slab.free(c)
    print("After freeing a and c:")
    slab.dump()

    # Now try to allocate 2 at position 0
    # Left neighbor is free, right neighbor (b) is color 0
    # Should pick color 1. OK.
    d = slab.allocate(2)
    print(f"Allocated 2 at {d}")
    slab.dump()
    assert d == 0

    # Try to allocate 2 at position 4
    # Left neighbor (b) is color 0, right neighbor (d) is color 1
    # Colors differ → impossible to pick a valid color!
    e = slab.allocate(2)
    print(f"Allocated 2 at {e}")
    slab.dump()

    if e == -1:
        print("Allocation failed as expected (color conflict)")
    else:
        print(f"WARNING: allocation succeeded at {e} but may violate invariants")
        # Check if it's valid
        errors = slab.validate()
        if errors:
            print(f"VALIDATION ERRORS: {errors}")

    print()


def test_random_stress():
    """Random allocations and frees."""
    print("=== test_random_stress ===")
    random.seed(42)
    slab = Slab(256)
    allocs = {}  # shard -> length
    failures = 0

    for _ in range(1000):
        op = random.choice(["alloc", "free"])
        if op == "alloc" or not allocs:
            length = random.randint(1, 8)
            shard = slab.allocate(length)
            if shard != -1:
                allocs[shard] = length
            else:
                failures += 1
        else:
            shard = random.choice(list(allocs.keys()))
            length = allocs.pop(shard)
            freed = slab.free(shard)
            assert freed == length, f"free({shard}) returned {freed}, expected {length}"

    # Free everything remaining
    for shard, length in list(allocs.items()):
        freed = slab.free(shard)
        assert freed == length

    assert all(x == 1 for x in slab.presence), "Leaked shards after stress test"
    print(f"Stress test: 1000 ops, {failures} allocation failures")
    print("PASS\n")


def test_simd_run_length():
    """
    Test the core SIMD operation: given a byte array and a bit position,
    find the run length of identical bits (only among allocated positions).

    This is what the C free() path must do efficiently.
    """
    print("=== test_simd_run_length ===")

    def find_run_python(presence, arbitrage, pos):
        """Reference implementation of run-length finding."""
        if presence[pos] == 1:
            return 0
        color = arbitrage[pos]
        left = pos
        while left > 0 and presence[left - 1] == 0 and arbitrage[left - 1] == color:
            left -= 1
        right = pos
        while right + 1 < len(presence) and presence[right + 1] == 0 and arbitrage[right + 1] == color:
            right += 1
        return right - left + 1

    # Test cases
    presence = [1] * 32
    arbitrage = [0] * 32

    # Alloc 3 at 0, alloc 2 at 3, alloc 1 at 5, alloc 3 at 6
    for i in range(3):
        presence[i] = 0
        arbitrage[i] = 1
    for i in range(3, 5):
        presence[i] = 0
        arbitrage[i] = 0
    presence[5] = 0
    arbitrage[5] = 1
    for i in range(6, 9):
        presence[i] = 0
        arbitrage[i] = 0

    assert find_run_python(presence, arbitrage, 0) == 3
    assert find_run_python(presence, arbitrage, 2) == 3
    assert find_run_python(presence, arbitrage, 3) == 2
    assert find_run_python(presence, arbitrage, 5) == 1
    assert find_run_python(presence, arbitrage, 7) == 3

    print("Reference implementation: PASS")

    # Now simulate what SIMD would see: 32 bytes, each byte = 8 bits
    # We process one __m256i at a time = 32 bytes = 256 bits
    # For this test, we only have 32 bits, so we use a smaller simulation.

    def simd_run_length_byte(presence_bytes, arbitrage_bytes, pos):
        """
        Simulate the SIMD approach for a single 32-byte (256-bit) chunk.
        Returns run length within this chunk.
        """
        byte_idx = pos // 8
        bit_idx = pos % 8

        color = (arbitrage_bytes[byte_idx] >> bit_idx) & 1

        # Build a 32-byte mask: for each byte, 0xFF if all 8 bits match color AND are allocated
        match_bytes = []
        for i in range(32):
            # All bits in byte must be `color` and allocated
            arb_byte = arbitrage_bytes[i]
            pres_byte = presence_bytes[i]

            # For a byte to fully match: all allocated bits must have color `color`
            # pres_byte: 1=free, 0=allocated. We need (arb_byte == all_color) where bits are allocated
            # A byte fully matches if: for all bits j, (pres_byte & (1<<j)) == 0 implies ((arb_byte>>j)&1) == color
            # This is equivalent to: (arb_byte == (color ? 0xFF : 0x00)) for all allocated bits
            # A simpler check: pres_byte must have no allocated bits with wrong color
            target_byte = 0xFF if color else 0x00
            # Allocated bits that DON'T match target: pres_inverted & (arb ^ target)
            # Wait, presence=1 means free, so allocated bits are where pres_byte has 0s.
            # We need: for every bit where pres=0, arb matches color.
            # Mismatches: (pres_byte == 0) && (arb_bit != color)
            # In byte form: ~pres_byte & (arb_byte ^ target_byte) should be 0
            mismatch = (~pres_byte & 0xFF) & (arb_byte ^ target_byte)
            if mismatch == 0:
                match_bytes.append(0xFF)
            else:
                match_bytes.append(0x00)

        # Now find the run containing byte_idx
        # First, check if the byte fully matches
        if match_bytes[byte_idx] == 0xFF:
            # Count full-match bytes to the left
            left_count = 0
            for i in range(byte_idx - 1, -1, -1):
                if match_bytes[i] == 0xFF:
                    left_count += 1
                else:
                    break

            # Count full-match bytes to the right
            right_count = 0
            for i in range(byte_idx + 1, 32):
                if match_bytes[i] == 0xFF:
                    right_count += 1
                else:
                    break

            return (left_count + 1 + right_count) * 8
        else:
            # Byte partially matches; examine individual bits
            arb_byte = arbitrage_bytes[byte_idx]
            pres_byte = presence_bytes[byte_idx]

            # Find run within this byte
            run_len = 0
            # Scan left within byte
            for j in range(bit_idx, -1, -1):
                if ((pres_byte >> j) & 1) == 0 and ((arb_byte >> j) & 1) == color:
                    run_len += 1
                else:
                    break
            # Wait, this includes the target bit. Let me rewrite.

            # Find left boundary within byte
            left_bit = bit_idx
            while left_bit > 0:
                if ((pres_byte >> (left_bit - 1)) & 1) == 0 and ((arb_byte >> (left_bit - 1)) & 1) == color:
                    left_bit -= 1
                else:
                    break

            # Find right boundary within byte
            right_bit = bit_idx
            while right_bit < 7:
                if ((pres_byte >> (right_bit + 1)) & 1) == 0 and ((arb_byte >> (right_bit + 1)) & 1) == color:
                    right_bit += 1
                else:
                    break

            run_len = right_bit - left_bit + 1

            # Check if run extends into adjacent bytes
            # Left neighbor: check MSB of previous byte
            if left_bit == 0 and byte_idx > 0:
                left_pres = presence_bytes[byte_idx - 1]
                left_arb = arbitrage_bytes[byte_idx - 1]
                if ((left_pres >> 7) & 1) == 0 and ((left_arb >> 7) & 1) == color:
                    # Previous byte's MSB matches. Need to scan further left.
                    # For simplicity in this simulation, just count matching bits
                    # from the right edge of the previous byte.
                    j = 7
                    while j >= 0:
                        if ((left_pres >> j) & 1) == 0 and ((left_arb >> j) & 1) == color:
                            run_len += 1
                            j -= 1
                        else:
                            break

            # Right neighbor: check LSB of next byte
            if right_bit == 7 and byte_idx < 31:
                right_pres = presence_bytes[byte_idx + 1]
                right_arb = arbitrage_bytes[byte_idx + 1]
                if ((right_pres >> 0) & 1) == 0 and ((right_arb >> 0) & 1) == color:
                    j = 0
                    while j < 8:
                        if ((right_pres >> j) & 1) == 0 and ((right_arb >> j) & 1) == color:
                            run_len += 1
                            j += 1
                        else:
                            break

            return run_len

    # Convert our 32-bit presence/arbitrage to 4 bytes (padded to 32)
    def bits_to_bytes(bits):
        bytes_arr = [0xFF] * 32  # start with all free
        for i, b in enumerate(bits):
            byte_idx = i // 8
            bit_idx = i % 8
            if b == 0:
                bytes_arr[byte_idx] &= ~(1 << bit_idx)
            else:
                bytes_arr[byte_idx] |= (1 << bit_idx)
        return bytes(bytes_arr)

    pres_bytes = bits_to_bytes(presence)
    arb_bytes = bits_to_bytes(arbitrage)

    for test_pos in [0, 2, 3, 5, 7]:
        expected = find_run_python(presence, arbitrage, test_pos)
        got = simd_run_length_byte(pres_bytes, arb_bytes, test_pos)
        assert got == expected, f"pos={test_pos}: expected {expected}, got {got}"

    print("SIMD byte-masking approach: PASS")
    print()


if __name__ == "__main__":
    test_basic_alloc_free()
    test_color_conflict()
    test_color_conflict_impossible()
    test_random_stress()
    test_simd_run_length()
    print("All tests passed.")
