# libc23

An experimental freestanding C23 library built with Clang. The current exercised
configuration is Windows x86_64; this project is incomplete and does not claim
full C23 conformance. See [docs/DESIGN.md](docs/DESIGN.md) for architecture and
build details, and [docs/STATUS.md](docs/STATUS.md) for implementation status
and known limits. The detailed standards inventory is in
[docs/C23-COVERAGE.md](docs/C23-COVERAGE.md); finite Windows x86_64 semantic
evidence is summarized in [docs/C23-COMPLETION.md](docs/C23-COMPLETION.md).
Supported implementation-defined Windows behavior is recorded in
[docs/IMPLEMENTATION-DEFINED.md](docs/IMPLEMENTATION-DEFINED.md).

## Build and verify

Run the full supported Windows x86_64 verification pipeline from the project root:

```powershell
python tools/verify_all.py
```

This runs release and debug test suites, public header and compile-fixture
checks, API audits, and the `c23cc` tgmath regression. Results and per-gate
logs are written under `build/verification/`.
The Windows test runner uses the direct executor by default. To build and run
only the tests for one configuration:

```powershell
python build.py test --platform windows --arch x86_64
```
