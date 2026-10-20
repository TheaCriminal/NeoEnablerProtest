# 13.60 padding preflight investigation

The reported diagnostic-v2 status recognized firmware 13.60 and selected DATA,
but refused before hook writes with `ShellCore padding occupied`.

- ShellCore base: `0x2b414000`.
- First nonzero prefix address: `0x2cd76700`, RVA `0x1962700`.
- Selected reservation pool: RVAs `[0x1963000, 0x1964000)`.
- All four 1024-byte slots in that pool reported zero nonzero bytes.
- The prefix contained x86-64 instructions. Its writer/owner was not established.
- The SDK object's purported text-size word was pointer-like, so it is reported
  as a raw metadata word and is not used as an executable-size boundary.

The original installer required the complete gap from the captured ELF's
file-backed code end (`0x19626fc`) to the reservation pool to be zero. That gap
is outside every write target. A modified runtime prefix can therefore refuse
an otherwise clear pool. The supplied decrypted ELF had zero padding; it did
not establish that the runtime prefix would remain unused by other payloads.
13.20 passed that original runtime check in the maintainer's test.

The test candidate removes only the prefix-zero prerequisite. The pool must
remain within the fixed firmware profile's final code page and outside its
captured file-backed code range. Every actual slot must still be either zero
or a precisely recognized, disarmed retained shim; retained slots are never
reused. An unknown/armed slot or unreadable slot still refuses installation.
The selected fresh slot, exact function/call/import/GOT/wrapper, RWX permissions
and suspension are still checked again before writes. No prefix byte is written.
Kernel DATA offsets and the extension transaction are unchanged. The original
firmware query is used for this investigation; the withdrawn cache change is
not part of this candidate.

GCC and Clang ASan/UBSan hook/controller/DATA tests pass. Native shim ABI and
barrier tests pass. The hook regression replays the reported 32 instruction
bytes before the 13.60 pool, verifies successful installation without changing
them, and separately verifies that nonzero data inside each slot causes refusal
before any write. Both firmware profiles also preserve a densely occupied prefix
through installation and removal.

Candidate name: `NeoEnabler 0.3.5-experimental-hook-padding-test`.
ELF SHA256: `719b15e08fd7b5fad3e9cface1611b3b2d3835733646276f968a9acf661bf655`.
This candidate has not yet been verified on the affected 13.60 console.
