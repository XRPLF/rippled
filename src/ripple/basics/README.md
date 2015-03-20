# Basics

Utility functions and classes.

ripple/basic should contain no dependencies on other modules.


Choosing a rippled container.
=============================

* `std::vector`
  * For ordered containers with most insertions or erases at the end.

* `std::deque`
  * For ordered containers with most insertions or erases at the start or end.

* `std::list`
  * For ordered containers with inserts and erases to the middle.
  * For containers with iterators stable over insert and erase.
  * Generally slower and bigger than `std::vector` or `std::deque` except for
    those cases.

* `std::set`
  * For sorted containers.

* `ripple::hash_set`
  * Where inserts and contains need to be O(1).
  * For "small" sets, `std::set` might be faster and smaller.

* `ripple::hardened_hash_set`
  * For data sets where the key could be manipulated by an attacker
    in an attempt to mount an algorithmic complexity attack:  see
    http://en.wikipedia.org/wiki/Algorithmic_complexity_attack


The following container is deprecated

* `std::unordered_set`
 * Use `ripple::hash_set` instead, which uses a better hashing algorithm.
 * Or use `ripple::hardened_hash_set` to prevent algorithmic complexity attacks.
