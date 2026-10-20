#!/usr/bin/env python3
"""Host-only native execution of the memory-step shim.

No console or network. Run tools/test_budget_shim.sh for a bounded outer
subprocess timeout; these tests execute generated assembly in the host process.
"""
import ctypes as C
import json
import mmap
import platform
import struct
import sys
import threading
import time
from pathlib import Path

BUILD = Path(sys.argv[1]).resolve()
CODE = (BUILD / 'step-shim.bin').read_bytes()
SYMS = json.loads((BUILD / 'step-symbols.json').read_text())
LIB = C.CDLL(str(BUILD / 'step-tests.so'))

ORIGINAL = C.CFUNCTYPE(C.c_uint64, C.c_void_p, C.c_void_p, C.c_uint64)
LIB.test_callee_saved.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint64]
LIB.test_callee_saved.restype = C.c_int

MS_EBUSY = 0x80020010
PHASE_POST = 1
PHASE_HELD = 2
PHASE_RUNNING = 3
PHASE_EXPIRED = 4
PHASE_FINISHED = 5


class Shim:
    def __init__(self):
        self.mapping = mmap.mmap(-1, 4096, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        self.mapping[:len(CODE)] = CODE
        self.base = C.addressof(C.c_char.from_buffer(self.mapping))
        self.fn = ORIGINAL(self.base)
        self.mount = C.create_string_buffer(struct.pack('<I', 0xf000) + b'CUSA01111\0' + bytes(32))
        self.json = C.create_string_buffer(b'{}')
        self.size_arg = 0x12345678
        self.calls = []
        self.return_value = 0x123456789abcdef0

        def original(mount, json_ptr, size):
            self.calls.append((mount, json_ptr, size))
            return self.return_value

        self.original_cb = ORIGINAL(original)
        self.set('original', C.cast(self.original_cb, C.c_void_p).value, 8)

    def get(self, field, size=4):
        return int.from_bytes(self.mapping[SYMS[field]:SYMS[field] + size], 'little')

    def set(self, field, value, size=4):
        self.mapping[SYMS[field]:SYMS[field] + size] = value.to_bytes(size, 'little', signed=False)

    def call(self):
        return self.fn(C.addressof(self.mount), C.addressof(self.json), self.size_arg)

    def spawn(self):
        output = []
        thread = threading.Thread(target=lambda: output.append(self.call()))
        thread.start()
        return thread, output

    def wait_seq(self, expected=1, timeout=2.0):
        deadline = time.monotonic() + timeout
        while self.get('seq') != expected and time.monotonic() < deadline:
            time.sleep(0.0001)
        return self.get('seq') == expected


def ensure_magic():
    assert bytes(bytearray(CODE[SYMS['magic']:SYMS['magic'] + 8])) == b'NEOMS001'
    assert len(CODE) <= 1024


def pass_through_disabled():
    s = Shim()
    assert s.call() == s.return_value
    assert s.get('seq') == 0 and s.get('active') == 0 and s.get('gate') == 0
    assert s.calls == [(C.addressof(s.mount), C.addressof(s.json), s.size_arg)]
    assert s.get('phase') == 0 and s.get('refused') == 0 and s.get('expired') == 0


def pass_through_null_mount():
    s = Shim()
    s.set('enabled', 1)
    s.set('wait_cycles', 5_000_000_000, 8)
    s.fn_null = ORIGINAL(s.base)
    result = s.fn_null(0, C.addressof(s.json), s.size_arg)
    assert result == s.return_value
    assert s.get('seq') == 0 and s.get('gate') == 0


def argument_forwarding_and_callee_saved():
    s = Shim()
    assert LIB.test_callee_saved(s.base, C.addressof(s.mount), C.addressof(s.json), s.size_arg) == 0
    assert s.calls == [(C.addressof(s.mount), C.addressof(s.json), s.size_arg)]
    # A native original stub that returns RSP & 15: the shim must enter with 8.
    # Place it past the shim payload so no instruction byte is overwritten.
    stub = bytes.fromhex('4889e083e00fc390')
    s.mapping[1024:1024 + len(stub)] = stub
    s.set('original', s.base + 1024, 8)
    assert s.call() == 8


def full_rax_return():
    s = Shim()
    s.return_value = 0xdeadbeefcafebabe
    assert s.call() == 0xdeadbeefcafebabe


def finished_cycle():
    for result in (40967, 0xfffffffffffff001):
        s = Shim(); s.return_value = result
        s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
        thread, output = s.spawn()
        assert s.wait_seq(1)
        assert s.get('gate') == 1 and s.get('phase') == PHASE_POST
        assert s.get('result') == result & 0xffffffff
        assert s.get('category') == 0xf000
        assert bytes(s.mapping[SYMS['title']:SYMS['title'] + 10]) == b'CUSA01111\0'
        s.set('response_seq', 1); s.set('command', 2)
        thread.join(3)
        assert not thread.is_alive() and output == [result]
        assert s.get('phase') == PHASE_FINISHED
        assert s.get('gate') == 0 and s.get('active') == 0


def post_expiration():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 0, 8)
    assert s.call() == s.return_value
    assert s.get('phase') == PHASE_EXPIRED and s.get('expired') == 1
    assert s.get('gate') == 0 and s.get('active') == 0


def late_claim_does_not_promote():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 0, 8)
    assert s.call() == s.return_value and s.get('phase') == PHASE_EXPIRED
    # Writing CLAIM after expiry must not escalate the gate or launch a HELD
    # window: the mailbox is already past its deadline and the shim returned.
    s.set('response_seq', 1); s.set('command', 1)
    time.sleep(0.01)
    assert s.get('phase') == PHASE_EXPIRED
    assert s.get('gate') == 0 and s.get('active') == 0


def held_has_no_timeout_then_running():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
    thread, output = s.spawn()
    assert s.wait_seq(1)
    s.set('response_seq', 1); s.set('command', 1)
    deadline = time.monotonic() + 0.4
    while s.get('phase') != PHASE_HELD and time.monotonic() < deadline:
        time.sleep(0.0005)
    assert s.get('phase') == PHASE_HELD
    # Shrinking the pre-publish deadline to zero must not escape HELD.
    s.set('wait_cycles', 0, 8)
    time.sleep(0.2)
    assert thread.is_alive() and s.get('phase') == PHASE_HELD
    s.set('command', 3)
    thread.join(3)
    assert not thread.is_alive() and output == [s.return_value]
    # The controller now owns the gate: it must remain claimed until it rolls back.
    assert s.get('phase') == PHASE_RUNNING
    assert s.get('gate') == 1 and s.get('active') == 0


def held_finished_by_release():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
    thread, output = s.spawn()
    assert s.wait_seq(1)
    s.set('response_seq', 1); s.set('command', 1)
    deadline = time.monotonic() + 0.4
    while s.get('phase') != PHASE_HELD and time.monotonic() < deadline:
        time.sleep(0.0005)
    assert s.get('phase') == PHASE_HELD
    s.set('command', 2)
    thread.join(3)
    assert not thread.is_alive() and output == [s.return_value]
    assert s.get('phase') == PHASE_FINISHED
    assert s.get('gate') == 0 and s.get('active') == 0


def stale_response_sequence_ignored():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
    thread, output = s.spawn()
    assert s.wait_seq(1)
    # A stale/foreign response_seq must not be mistaken for an answer.
    s.set('response_seq', 7)
    s.set('command', 2)
    time.sleep(0.05)
    assert thread.is_alive() and s.get('phase') == PHASE_POST
    s.set('response_seq', 1)
    thread.join(3)
    assert not thread.is_alive() and output == [s.return_value]
    assert s.get('phase') == PHASE_FINISHED


def foreign_and_invalid_commands_ignored():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
    # Old mailbox contents must be cleared before publishing a new sequence.
    s.set('response_seq', 1); s.set('command', 3)
    thread, output = s.spawn()
    assert s.wait_seq(1)
    assert s.get('response_seq') == 0 and s.get('command') == 0
    # RUNNING is legal only after CLAIM, and unknown commands cannot release POST.
    s.set('response_seq', 1); s.set('command', 3)
    time.sleep(0.02)
    assert thread.is_alive() and s.get('phase') == PHASE_POST
    s.set('command', 99)
    time.sleep(0.02)
    assert thread.is_alive() and s.get('phase') == PHASE_POST
    s.set('command', 1)
    deadline = time.monotonic() + 0.4
    while s.get('phase') != PHASE_HELD and time.monotonic() < deadline:
        time.sleep(0.0005)
    assert s.get('phase') == PHASE_HELD
    s.set('response_seq', 7); s.set('command', 3)
    time.sleep(0.02)
    assert thread.is_alive() and s.get('phase') == PHASE_HELD
    # Publish the command before its sequence so the previous RUNNING value
    # cannot become valid during the controller's two mailbox writes.
    s.set('command', 2); s.set('response_seq', 1)
    thread.join(3)
    assert not thread.is_alive() and output == [s.return_value]
    assert s.get('phase') == PHASE_FINISHED and s.get('gate') == 0


def contention_counters():
    # Park the first thread in HELD so the gate is held but no POST deadline
    # can race the contender; then wait_cycles=0 bounds the contender.
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 5_000_000_000, 8)
    thread, output = s.spawn()
    assert s.wait_seq(1)
    s.set('response_seq', 1); s.set('command', 1)
    deadline = time.monotonic() + 0.4
    while s.get('phase') != PHASE_HELD and time.monotonic() < deadline:
        time.sleep(0.0005)
    assert s.get('phase') == PHASE_HELD and s.get('gate') == 1
    s.set('wait_cycles', 0, 8)
    assert s.call() == MS_EBUSY
    assert s.get('refused') == 1 and s.get('blocked') >= 1
    # Release the first thread cleanly.
    s.set('command', 2)
    thread.join(3)
    assert not thread.is_alive() and output == [s.return_value]
    assert s.get('gate') == 0


def disarm_mid_call():
    s = Shim()
    s.set('enabled', 1); s.set('wait_cycles', 0, 8)
    assert s.call() == s.return_value
    # Shim disabled while the original API was already called: nothing publishes.
    s.set('enabled', 0)
    before = s.get('seq')
    assert s.call() == s.return_value and s.get('seq') == before


def main():
    assert platform.machine().lower() in ('x86_64', 'amd64')
    ensure_magic()
    pass_through_disabled()
    pass_through_null_mount()
    argument_forwarding_and_callee_saved()
    full_rax_return()
    finished_cycle()
    post_expiration()
    late_claim_does_not_promote()
    held_has_no_timeout_then_running()
    held_finished_by_release()
    stale_response_sequence_ignored()
    foreign_and_invalid_commands_ignored()
    contention_counters()
    disarm_mid_call()
    print('PASS: step shim ABI, forwarding, finished cycle, POST expiry, late CLAIM refusal,')
    print('PASS: HELD without timeout, RUNNING gate retention, stale/foreign sequences,')
    print('PASS: invalid command refusal, mailbox clearing, contention and disarm')


if __name__ == '__main__':
    main()
