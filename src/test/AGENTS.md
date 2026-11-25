# AGENTS.md — test

See [README.md](./README.md) for basic `--unittest` invocation.

Don't copy-paste test helpers across test files. Shared transaction-building helpers belong in `jtx/`. Other shared code can stay near its users (e.g. `unit_test/SuiteJournal.h`, `app/lending/LoanTestBase.h`).
