1.0.0-b6

* Fix incorrect file deletion in create()

---

1.0.0-b5

* fail_file also fails on reads
* Fix bug in rekey where an error code wasn't checked
* Increase coverage
* Add buffer unit test
* Add is_File concept and checks
* Update documentation
* Add example program
* Demote exceptions to asserts in gentex
* Improved commit process
* Dynamic block size in custom allocator

---

1.0.0-b4

* Improved test coverage
* Use master branch for codecov badge
* Throw on API calls when no database open
* Benchmarks vs. RocksDB

### API Changes:

* `insert` sets `error::key_exists` instead of returning `false`
* `fetch` sets `error::key_not_found` instead of returning `false`

---

1.0.0-b3

* Tune buffer sizes for performance
* Fix large POSIX and Win32 writes
* Adjust progress indicator for nudb tool
* Document link requirements
* Add visit test
* Improved coverage

---

1.0.0-b2

* Minor documentation and tidying
* Add CHANGELOG

---

1.0.0-b1

* Initial source tree


