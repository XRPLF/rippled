JSON structural diff
====================

Does exactly what you think it does:

![Screenshot](https://github.com/andreyvit/json-diff/raw/master/doc/screenshot.png)


Installation
------------

    npm install json-diff


Usage
-----

Simple:

    json-diff a.json b.json

Detailed:

    % json-diff --help
    Usage: json-diff [-vjC] first.json second.json

    Arguments:
      first.json            Old file
      second.json           New file

    General options:
      -v, --verbose         Output progress info
      -C, --[no-]color      Colored output
      -j, --raw-json        Display raw JSON encoding of the diff
      -h, --help            Display this usage information


Features
--------

* colorized, diff-like output
* fuzzy matching of modified array elements (when array elements are object hierarchies)
* reasonable test coverage (far from 100%, though)


Tests
-----

Run:

    npm test

Test coverage report:

    npm run-script cov

Output:

    colorize
      ✓ should return ' <value>' for a scalar value
      ✓ should return '-<old value>', '+<new value>' for a scalar diff
      ✓ should return '-<removed key>: <removed value>' for an object diff with a removed key
      ✓ should return '+<added key>: <added value>' for an object diff with an added key
      ✓ should return '+<added key>: <added stringified value>' for an object diff with an added key and a non-scalar value
      ✓ should return ' <modified key>: <colorized diff>' for an object diff with a modified key
      ✓ should return '+<inserted item>' for an array diff
      ✓ should return '-<deleted item>' for an array diff

    diff
      with simple scalar values
        ✓ should return undefined for two identical numbers
        ✓ should return undefined for two identical strings
        ✓ should return { __old: <old value>, __new: <new value> } object for two different numbers
      with objects
        ✓ should return undefined for two objects with identical contents
        ✓ should return undefined for two object hierarchies with identical contents
        ✓ should return { <key>__deleted: <old value> } when the second object is missing a key
        ✓ should return { <key>__added: <new value> } when the first object is missing a key
        ✓ should return { <key>: { __old: <old value>, __new: <new value> } } for two objects with diffent scalar values for a key
        ✓ should return { <key>: <diff> } with a recursive diff for two objects with diffent values for a key
      with arrays of scalars
        ✓ should return undefined for two arrays with identical contents
        ✓ should return [..., ['-', <removed item>], ...] for two arrays when the second array is missing a value
        ✓ should return [..., ['+', <added item>], ...] for two arrays when the second one has an extra value
        ✓ should return [..., ['+', <added item>]] for two arrays when the second one has an extra value at the end (edge case test)
      with arrays of objects
        ✓ should return undefined for two arrays with identical contents
        ✓ should return [..., ['-', <removed item>], ...] for two arrays when the second array is missing a value
        ✓ should return [..., ['+', <added item>], ...] for two arrays when the second array has an extra value
        ✓ should return [..., ['~', <diff>], ...] for two arrays when an item has been modified (note: involves a crazy heuristic)

    ✔ 25 tests complete (12ms)


License
-------

© Andrey Tarantsov. Distributed under the MIT license.
