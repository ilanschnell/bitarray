"""
Stress tests for bitarray on a free-threaded CPython build.

This module is only imported (and its tests only added to the suite) when
Python was configured using --disable-gil.  That is:

    sysconfig.get_config_var("Py_GIL_DISABLED")

returns 1.

BITARRAY_TD_ROUNDS, BITARRAY_TD_NBITS, and BITARRAY_TD_TIMEOUT may be used
to increase the workload or timeout for longer stress runs.
"""
import gc
import io
import operator
import os
import queue
import sys
import sysconfig
import threading
import time
import traceback
import unittest
import weakref

assert sysconfig.get_config_var("Py_GIL_DISABLED")

from bitarray import bitarray, decodetree, frozenbitarray
from bitarray.util import _ssqi  # type: ignore
from bitarray.util import (
    any_and, ba2base, ba2hex, base2ba, byteswap,
    canonical_decode, correspond_all, count_and, count_n,
    count_or, count_xor, deserialize, hex2ba, parity,
    sc_decode, sc_encode, serialize, subset,
    vl_decode, vl_encode, xor_indices,
)

ROUNDS = int(os.environ.get("BITARRAY_TD_ROUNDS", "100"))
NBITS = int(os.environ.get("BITARRAY_TD_NBITS", str(1 << 15)))
TIMEOUT = float(os.environ.get("BITARRAY_TD_TIMEOUT", "30"))


class FreeThreadedStressTests(unittest.TestCase):

    def setUp(self):
        self.assertFalse(
            sys._is_gil_enabled(),
            "the GIL is enabled; bitarray may not have declared "
            "free-threading support",
        )
        self.assertGreater(ROUNDS, 0)
        self.assertGreaterEqual(NBITS, 8)
        self.assertEqual(NBITS % 8, 0)

    def run_workers(self, *workers):
        "Start workers together and report exceptions or deadlocks."
        barrier = threading.Barrier(len(workers) + 1)
        failures = queue.Queue()

        def run(worker):
            try:
                barrier.wait()
                worker()
            except BaseException:
                failures.put(traceback.format_exc())

        threads = [
            threading.Thread(target=run, args=(worker,), daemon=True)
            for worker in workers
        ]
        for thread in threads:
            thread.start()

        barrier.wait()
        deadline = time.monotonic() + TIMEOUT
        for thread in threads:
            thread.join(max(0.0, deadline - time.monotonic()))

        alive = [thread.name for thread in threads if thread.is_alive()]
        if alive:
            self.fail(f"worker deadlock or timeout: {', '.join(alive)}")

        messages = []
        while not failures.empty():
            messages.append(failures.get_nowait())
        if messages:
            self.fail("\n\n".join(messages))

    @staticmethod
    def yield_periodically(i):
        if i % 8 == 0:
            time.sleep(0)

    def assert_invariants(self, a):
        self.assertEqual(a.nbytes, (len(a) + 7) // 8)
        self.assertEqual(a.padbits, 8 * a.nbytes - len(a))
        self.assertGreaterEqual(a.padbits, 0)
        self.assertLessEqual(a.padbits, 7)
        self.assertEqual(a, a.copy())

    def test_gil_stays_disabled(self):
        self.assertFalse(sys._is_gil_enabled())

    def test_fixed_size_atomic_snapshots(self):
        "Readers must never observe a partially completed setall()."
        a = bitarray(NBITS)
        a.setall(0)
        raw_zero = bytes(NBITS // 8)
        raw_one = b"\xff" * (NBITS // 8)
        unpack_zero = bytes(NBITS)
        unpack_one = b"\x01" * NBITS
        str_zero = "0" * NBITS
        str_one = "1" * NBITS
        repr_zero = f"bitarray('{str_zero}')"
        repr_one = f"bitarray('{str_one}')"

        def writer():
            for i in range(ROUNDS * 2):
                a.setall(i & 1)
                self.yield_periodically(i)

        def bytes_reader():
            for i in range(ROUNDS):
                self.assertIn(a.tobytes(), (raw_zero, raw_one))
                self.assertIn(a.unpack(), (unpack_zero, unpack_one))
                self.yield_periodically(i)

        def text_reader():
            for i in range(ROUNDS):
                self.assertIn(a.to01(), (str_zero, str_one))
                self.assertIn(repr(a), (repr_zero, repr_one))
                self.yield_periodically(i)

        def list_reader():
            for i in range(ROUNDS):
                values = a.tolist()
                self.assertEqual(len(values), NBITS)
                self.assertIn(sum(values), (0, NBITS))
                self.assertIn(a.count(), (0, NBITS))
                self.yield_periodically(i)

        self.run_workers(writer, bytes_reader, text_reader, list_reader)
        self.assert_invariants(a)

    def test_fixed_size_util_snapshots(self):
        "Utility encoders must serialize one coherent bitarray state."
        a = bitarray(NBITS)
        a.setall(0)

        def writer():
            for i in range(ROUNDS * 2):
                a.setall(i & 1)
                self.yield_periodically(i)

        def reader():
            codecs = (
                (serialize, deserialize),
                (sc_encode, sc_decode),
                (vl_encode, vl_decode),
            )
            for i in range(ROUNDS):
                for encode, decode in codecs:
                    decoded = decode(encode(a))
                    self.assertEqual(len(decoded), NBITS)
                    self.assertIn(decoded.count(), (0, NBITS))
                self.yield_periodically(i)

        self.run_workers(writer, reader, reader)
        self.assert_invariants(a)

    def test_resize_atomic_snapshots(self):
        "A clear/extend writer exposes either the empty or complete state."
        pattern = "01101001" * (NBITS // 8)
        full = bitarray(pattern)
        a = full.copy()
        raw = full.tobytes()
        unpacked = full.unpack()
        full_repr = repr(full)
        full_list = full.tolist()

        def writer():
            for i in range(ROUNDS * 2):
                a.clear()
                a.extend(full)
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS):
                self.assertIn(a.to01(), ("", pattern))
                self.assertIn(a.tobytes(), (b"", raw))
                self.assertIn(a.unpack(), (b"", unpacked))
                self.assertIn(repr(a), ("bitarray()", full_repr))

                values = a.tolist()
                self.assertIn(len(values), (0, NBITS))
                if values:
                    self.assertEqual(values, full_list)
                self.yield_periodically(i)

        self.run_workers(writer, reader, reader)
        self.assert_invariants(a)

    def test_mixed_readers_and_mutators_do_not_crash(self):
        "Exercise resizing and in-place operations on one shared bitarray."
        a = bitarray("01101001" * 64)
        source = bitarray("10110010" * 32)
        expected = (IndexError, ValueError, BufferError, RuntimeError)

        def tolerate(operation):
            try:
                operation()
            except expected:
                pass

        def mutator():
            operations = (
                lambda: a.append(1),
                a.pop,
                lambda: a.insert(0, 1),
                lambda: a.remove(1),
                lambda: a.extend(source),
                a.clear,
                a.fill,
                a.invert,
                a.reverse,
                lambda: a.rotate(17),
                lambda: a.setall(0),
                lambda: a.sort(reverse=True),
                lambda: operator.ilshift(a, 3),
                lambda: operator.irshift(a, 3),
                lambda: operator.imul(a, 2),
                lambda: a.__setitem__(slice(None, None, 3), 1),
                lambda: a.__delitem__(slice(None, None, 5)),
            )
            for i in range(ROUNDS * 4):
                tolerate(operations[i % len(operations)])
                if len(a) > 4 * NBITS:
                    tolerate(a.clear)
                self.yield_periodically(i)

        def reader():
            operations = (
                lambda: len(a),
                a.all,
                a.any,
                a.count,
                lambda: a.find(1),
                lambda: a.index(1),
                a.tobytes,
                lambda: bytes(a),
                a.to01,
                a.tolist,
                a.unpack,
                lambda: repr(a),
                lambda: a.copy(),
                lambda: a.__reduce__(),
                lambda: a.__sizeof__(),
                lambda: operator.getitem(a, slice(None, None, -1)),
                lambda: operator.lshift(a, 3),
                lambda: operator.rshift(a, 3),
                lambda: operator.invert(a),
                lambda: operator.mul(a, 2),
                lambda: a.buffer_info(),
                lambda: serialize(a),
                lambda: sc_encode(a),
                lambda: vl_encode(a),
            )
            for i in range(ROUNDS * 3):
                tolerate(operations[i % len(operations)])
                self.yield_periodically(i)

        self.run_workers(mutator, mutator, reader, reader)
        self.assert_invariants(a)

    def test_iterators_while_source_changes(self):
        "Concurrent source mutation may alter results, but must remain safe."
        # Iterator next() methods acquire locks for every item.  Keep this
        # source small so BITARRAY_TD_ROUNDS controls the stress duration.
        iterator_nbits = min(NBITS, 256)
        full = bitarray("01" * (iterator_nbits // 2))
        a = full.copy()
        code = {0: bitarray("0"), 1: bitarray("1")}
        expected = (IndexError, ValueError, RuntimeError)

        def writer():
            for i in range(ROUNDS * 2):
                a.clear()
                a.extend(full)
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS):
                try:
                    self.assertTrue(all(value in (0, 1) for value in a))
                    self.assertTrue(all(index >= 0 for index in a.search(1)))
                    self.assertTrue(
                        all(symbol in (0, 1) for symbol in a.decode(code))
                    )
                except expected:
                    pass
                self.yield_periodically(i)

        self.run_workers(writer, reader, reader)
        self.assert_invariants(a)

    def test_one_iterator_consumed_by_many_threads(self):
        "The bitarray iterator's position must be updated exactly once."
        a = bitarray("01" * (NBITS // 2))
        iterator = iter(a)
        results = [[] for _ in range(4)]

        def consumer(result):
            while True:
                try:
                    result.append(next(iterator))
                except StopIteration:
                    return

        self.run_workers(
            *(lambda result=result: consumer(result) for result in results)
        )
        combined = [value for result in results for value in result]
        self.assertEqual(len(combined), NBITS)
        self.assertEqual(sum(combined), NBITS // 2)

    def test_two_bitarray_operations_in_opposite_order(self):
        "Two-object operations must not deadlock when argument order differs."
        nbits = min(NBITS, 1024)
        a = bitarray("01101001" * (nbits // 8))
        b = bitarray("10110010" * (nbits // 8))

        def worker(x, y):
            for i in range(ROUNDS):
                count_and(x, y)
                count_or(x, y)
                count_xor(x, y)
                any_and(x, y)
                subset(x, y)
                correspond_all(x, y)
                operator.and_(x, y)
                operator.or_(x, y)
                operator.xor(x, y)
                operator.add(x, y)
                operator.eq(x, y)
                operator.setitem(x, slice(None), y)

                if i % 3 == 0:
                    x ^= y
                elif i % 3 == 1:
                    x |= y
                else:
                    x &= y
                self.yield_periodically(i)

        self.run_workers(
            lambda: worker(a, b),
            lambda: worker(b, a),
        )
        self.assert_invariants(a)
        self.assert_invariants(b)
        self.assertEqual(len(a), nbits)
        self.assertEqual(len(b), nbits)

    def test_mask_and_sequence_indexing_under_mutation(self):
        "Exercise the three-object mask and sequence-indexing paths."
        nbits = min(NBITS, 256)
        base = bitarray("01101001" * (nbits // 8))
        target = base.copy()
        mask = bitarray("10" * (nbits // 2))
        other = bitarray("01" * (nbits // 2))
        indices = list(range(0, nbits, 2))
        expected = (IndexError, TypeError, ValueError, RuntimeError)

        def tolerate(operation):
            try:
                operation()
            except expected:
                pass

        def reset_target():
            target.clear()
            target.extend(base)

        def target_mutator():
            operations = (
                reset_target,
                target.clear,
                target.reverse,
                target.invert,
                lambda: target.append(1),
                lambda: target.pop(),
            )
            for i in range(ROUNDS * 2):
                tolerate(operations[i % len(operations)])
                self.yield_periodically(i)

        def input_mutator():
            for i in range(ROUNDS * 2):
                mask.setall(i & 1)
                mask.invert()
                other.clear()
                other.extend(base if i & 1 else mask)
                if i % 2:
                    indices[:] = range(0, nbits, 2)
                else:
                    indices[:] = range(nbits - 1, -1, -2)
                self.yield_periodically(i)

        def indexer():
            operations = (
                lambda: operator.getitem(target, mask),
                lambda: operator.getitem(target, indices),
                lambda: operator.setitem(target, mask, other),
                lambda: operator.setitem(target, indices, other),
                lambda: operator.setitem(target, mask, 1),
                lambda: operator.setitem(target, indices, 0),
                lambda: operator.delitem(target, mask),
                lambda: operator.delitem(target, indices),
                lambda: operator.setitem(target, -1, 1),
                lambda: operator.delitem(target, 0),
            )
            for i in range(ROUNDS * 3):
                tolerate(operations[i % len(operations)])
                self.yield_periodically(i)

        self.run_workers(target_mutator, input_mutator, indexer, indexer)
        self.assert_invariants(target)
        self.assert_invariants(mask)
        self.assert_invariants(other)

    def test_buffer_exports_during_resize(self):
        "Buffer exports must remain valid while resize attempts occur."
        nbits = min(NBITS, 1024)
        base = bitarray("01101001" * (nbits // 8))
        a = base.copy()

        def holder():
            for i in range(ROUNDS * 2):
                view = memoryview(a)
                try:
                    if view.nbytes:
                        view[0] ^= 0xff
                    self.yield_periodically(i)
                finally:
                    view.release()

        def resizer():
            operations = (
                lambda: a.append(1),
                a.pop,
                a.clear,
                lambda: a.extend(base),
                lambda: a.frombytes(b"\xaa\x55"),
                lambda: a.pack(b"\x00\x01\x00\x01"),
            )
            for i in range(ROUNDS * 3):
                try:
                    operations[i % len(operations)]()
                except (BufferError, IndexError, RuntimeError):
                    pass
                if len(a) > 4 * nbits:
                    try:
                        a.clear()
                    except BufferError:
                        pass
                self.yield_periodically(i)

        self.run_workers(holder, holder, resizer)
        self.assert_invariants(a)

    def test_unrelated_buffer_aliases_do_not_crash(self):
        "Independent locks cannot ensure coherent aliases, only memory safety."
        nbytes = min(NBITS // 8, 128)
        swap_size = 2 if nbytes % 2 == 0 else 1
        backing = bytearray(nbytes)
        a = bitarray(buffer=backing)
        b = bitarray(buffer=backing)

        def bitarray_writer(x):
            operations = (
                x.invert,
                x.reverse,
                lambda: x.setall(0),
                lambda: x.setall(1),
                x.bytereverse,
            )
            for i in range(ROUNDS * 2):
                operations[i % len(operations)]()
                self.yield_periodically(i)

        def buffer_writer():
            for i in range(ROUNDS * 3):
                backing[i % nbytes] ^= 0xff
                byteswap(backing, swap_size)
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS * 2):
                a.tobytes()
                b.to01()
                a.count()
                b.buffer_info()
                self.yield_periodically(i)

        self.run_workers(
            lambda: bitarray_writer(a),
            lambda: bitarray_writer(b),
            buffer_writer,
            reader,
        )
        self.assertEqual(len(a), 8 * nbytes)
        self.assertEqual(len(b), 8 * nbytes)
        self.assert_invariants(a)
        self.assert_invariants(b)

    def test_file_methods_and_reentrant_streams(self):
        "File callbacks may execute Python and mutate the same bitarray."
        nbits = min(NBITS, 1024)
        base = bitarray("01101001" * (nbits // 8))
        a = base.copy()
        payload = base.tobytes()
        expected = (BufferError, EOFError, IndexError, RuntimeError)

        class ReentrantWriter:
            def write(self, block):
                a.invert()
                return len(block)

        class ReentrantReader:
            def __init__(self):
                self.done = False

            def read(self, n):
                a.reverse()
                if self.done:
                    return b""
                self.done = True
                return payload[:n]

        def tolerate(operation):
            try:
                operation()
            except expected:
                pass

        def mutator():
            for i in range(ROUNDS * 2):
                tolerate(a.clear)
                tolerate(lambda: a.extend(base))
                self.yield_periodically(i)

        def file_worker():
            for i in range(ROUNDS):
                tolerate(lambda: a.tofile(io.BytesIO()))
                tolerate(lambda: a.tofile(ReentrantWriter()))
                tolerate(lambda: a.fromfile(io.BytesIO(payload)))
                tolerate(lambda: a.fromfile(ReentrantReader()))
                if len(a) > 4 * nbits:
                    tolerate(a.clear)
                self.yield_periodically(i)

        self.run_workers(mutator, file_worker, file_worker)
        self.assert_invariants(a)

    def test_shared_search_decode_and_canonical_iterators(self):
        "Shared iterator objects must remain valid under concurrent next()."
        nbits = min(NBITS, 256)
        a = bitarray("01" * (nbits // 2))
        code = {0: bitarray("0"), 1: bitarray("1")}

        search = a.search(1)
        search_results = [[] for _ in range(4)]

        def consume(iterator, result):
            while True:
                try:
                    result.append(next(iterator))
                except StopIteration:
                    return

        self.run_workers(
            *(lambda result=result: consume(search, result)
              for result in search_results)
        )
        positions = [
            position for result in search_results for position in result
        ]
        # searchiterator does not promise exact-once delivery when one
        # iterator is consumed concurrently.
        for position in positions:
            self.assertGreaterEqual(position, 0)
            self.assertLess(position, nbits)
            self.assertEqual(a[position], 1)

        decode = a.decode(code)
        decode_results = [[] for _ in range(4)]
        self.run_workers(
            *(lambda result=result: consume(decode, result)
              for result in decode_results)
        )
        decoded = [value for result in decode_results for value in result]
        self.assertEqual(len(decoded), nbits)
        self.assertEqual(sum(decoded), nbits // 2)

        canonical = canonical_decode(a, [0, 2], [0, 1])
        canonical_results = [[] for _ in range(4)]
        self.run_workers(
            *(lambda result=result: consume(canonical, result)
              for result in canonical_results)
        )
        decoded = [value for result in canonical_results for value in result]
        self.assertEqual(len(decoded), nbits)
        self.assertEqual(sum(decoded), nbits // 2)

    def test_decode_iterator_skipbits_concurrently(self):
        "next(), skipbits(), and index share one decode iterator safely."
        nbits = min(NBITS, 256)
        a = bitarray("01" * (nbits // 2))
        code = {0: bitarray("0"), 1: bitarray("1")}
        iterator = a.decode(code)
        consumed = [[] for _ in range(2)]
        skipped = []

        def consumer(result):
            while True:
                try:
                    result.append(next(iterator))
                except StopIteration:
                    return

        def skipper():
            while True:
                try:
                    skipped.append(len(iterator.skipbits(1)))
                    self.assertGreaterEqual(iterator.index, 0)
                    self.assertLessEqual(iterator.index, nbits)
                except ValueError:
                    return

        self.run_workers(
            lambda: consumer(consumed[0]),
            lambda: consumer(consumed[1]),
            skipper,
        )
        self.assertEqual(
            sum(map(len, consumed)) + sum(skipped),
            nbits,
        )
        self.assertEqual(iterator.index, nbits)

    def test_remaining_utility_readers(self):
        "Exercise unary utilities and text conversions during mutation."
        nbits = min(NBITS, 1024)
        a = bitarray(nbits)
        a.setall(0)
        hex_zero = "0" * (nbits // 4)
        hex_one = "f" * (nbits // 4)
        base_zero = "0" * nbits
        base_one = "1" * nbits

        def writer():
            for i in range(ROUNDS * 2):
                a.setall(i & 1)
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS):
                self.assertIn(ba2hex(a), (hex_zero, hex_one))
                self.assertIn(ba2base(2, a), (base_zero, base_one))
                self.assertIn(parity(a), (0, 1))
                self.assertIsInstance(xor_indices(a), int)
                self.assertIsInstance(_ssqi(a), int)
                self.assertIsInstance(_ssqi(a, 2), int)
                self.assertEqual(count_n(a, 0), 0)
                self.yield_periodically(i)

        self.run_workers(writer, reader, reader)
        self.assert_invariants(a)

    def test_text_decoders_with_mutable_exporter(self):
        "Text decoders must protect a mutable object exporting its buffer."
        nbytes = min(NBITS // 8, 128)
        text = bytearray(b"0" * nbytes)

        def writer():
            zero = b"0" * nbytes
            one = b"1" * nbytes
            for i in range(ROUNDS * 2):
                text[:] = one if i & 1 else zero
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS * 2):
                a = hex2ba(text)
                b = base2ba(2, text)
                self.assertEqual(len(a), 4 * nbytes)
                self.assertEqual(len(b), nbytes)
                self.assertIn(a.count(), (0, nbytes))
                self.assertIn(b.count(), (0, nbytes))
                self.yield_periodically(i)

        self.run_workers(writer, reader, reader)

    def test_subclass_finalizer_reenters_bitarray(self):
        "Destroying overlap copies must not run a finalizer under the lock."
        nbits = min(NBITS, 256)
        failures = []
        weak_callbacks = []

        class ReentrantBitarray(bitarray):
            target = None
            finalized = 0

            def __del__(self):
                type(self).finalized += 1
                target = type(self).target
                if target is not None:
                    try:
                        target.count()
                    except BaseException:
                        failures.append(traceback.format_exc())

        a = ReentrantBitarray("01101001" * (nbits // 8))
        ReentrantBitarray.target = a

        def assigner():
            for i in range(ROUNDS * 2):
                operator.setitem(a, slice(None), a)
                self.yield_periodically(i)

        def collector():
            for i in range(ROUNDS):
                gc.collect()
                self.yield_periodically(i)

        def weak_callback(_):
            try:
                a.count()
                weak_callbacks.append(1)
            except BaseException:
                failures.append(traceback.format_exc())

        def weakref_worker():
            for i in range(ROUNDS):
                tmp = a.copy()
                ref = weakref.ref(tmp, weak_callback)
                del tmp
                self.assertIsNone(ref())
                self.yield_periodically(i)

        self.run_workers(assigner, assigner, collector, weakref_worker)
        ReentrantBitarray.target = None
        gc.collect()

        self.assertFalse(failures)
        self.assertGreater(ReentrantBitarray.finalized, 0)
        self.assertGreater(len(weak_callbacks), 0)
        self.assert_invariants(a)

    def test_readonly_objects_under_concurrent_access(self):
        "Immutable and read-only bitarrays remain stable under contention."
        nbits = min(NBITS, 1024)
        pattern = bitarray("01101001" * (nbits // 8))
        objects = (
            frozenbitarray(pattern),
            bitarray(buffer=bytes(pattern.tobytes())),
        )
        expected_count = pattern.count()
        expected_bytes = pattern.tobytes()

        def reader():
            for i in range(ROUNDS):
                for a in objects:
                    self.assertEqual(a.count(), expected_count)
                    self.assertEqual(a.tobytes(), expected_bytes)
                    self.assertEqual(a[:], pattern)
                    if isinstance(a, frozenbitarray):
                        hash(a)
                self.yield_periodically(i)

        def rejected_writer():
            for i in range(ROUNDS):
                for a in objects:
                    operations = (
                        lambda: a.setall(0),
                        a.invert,
                        lambda: a.append(0),
                        lambda: operator.setitem(a, 0, 1),
                    )
                    for operation in operations:
                        with self.assertRaises((TypeError, BufferError)):
                            operation()
                self.yield_periodically(i)

        self.run_workers(reader, reader, rejected_writer)

    def test_codedict_mutation_does_not_crash(self):
        "Tree construction must tolerate a concurrently changing dictionary."
        zero = bitarray("0")
        one = bitarray("1")
        code = {0: zero, 1: one}
        valid = {0: zero, 1: one}
        data = bitarray("01101001" * 64)

        def writer():
            for i in range(ROUNDS * 4):
                code.clear()
                code.update(valid)
                self.yield_periodically(i)

        def value_writer():
            for i in range(ROUNDS * 4):
                zero.clear()
                zero.append(0)
                one.clear()
                one.append(1)
                self.yield_periodically(i)

        def reader():
            for i in range(ROUNDS * 2):
                try:
                    tree = decodetree(code)
                    tree.nodes()
                    tree.todict()
                    target = bitarray()
                    target.encode(code, (0, 1, 1, 0))
                    list(data.decode(tree))
                except (KeyError, TypeError, ValueError, RuntimeError):
                    pass
                self.yield_periodically(i)

        self.run_workers(writer, value_writer, reader, reader)


if __name__ == "__main__":
    unittest.main(verbosity=2)
