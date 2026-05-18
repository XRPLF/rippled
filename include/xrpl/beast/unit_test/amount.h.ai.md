# `beast/unit_test/amount.h`

## Role and Purpose

`amount.h` is a minimal formatting utility inside the `beast::unit_test` framework. Its sole job is to render a count and a unit label as a grammatically correct English phrase, automatically pluralizing the label when the count is not exactly one. For example, it turns `(1, "suite")` into `"1 suite"` and `(3, "suite")` into `"3 suites"`. The class exists because the test reporter's final summary line repeatedly formats counts of suites, cases, tests, and failures — keeping that logic centralized in a tiny helper avoids repetition and keeps the output consistent.

## Design of the `amount` Class

The class stores a `std::size_t` count (`n_`) and a **reference** to a `std::string` label (`what_`). Holding a reference rather than a copy is intentional: `amount` objects are meant to be short-lived temporaries created inline within a stream expression, so the referent (typically a string literal wrapped in a `std::string`) always outlives the `amount`. This is why copy-assignment (`operator=`) is explicitly deleted — assigning an `amount` that holds a reference to another object's string would be semantically dangerous, and the type is not meant to be stored or reassigned anyway. The copy constructor is defaulted for passing by value if needed, but in practice all uses in the codebase construct `amount` directly in stream expressions.

The constructor uses a dummy template parameter (`template <class = void>`) rather than being defined as a plain `inline` function. This pattern is a way to provide a function definition in a header file without the `inline` keyword while still satisfying the One Definition Rule. Because a function template is implicitly `inline`-equivalent for linkage purposes, this avoids multiple-definition linker errors when the header is included in multiple translation units.

## Pluralization Logic

The `operator<<` overload implements the formatting:

```cpp
s << t.n_ << " " << t.what_ << ((t.n_ != 1) ? "s" : "");
```

This appends a literal `"s"` suffix for any count other than 1. The approach assumes English regular pluralization and works correctly for all four unit labels used in practice: `"suite"`, `"case"`, `"test"`, and `"failure"`.

## Relationship to `reporter.h`

The only consumer of `amount` within this codebase is `reporter.h`, in the destructor of the `reporter` class template:

```cpp
os_ << fmtdur(elapsed) << ", " << amount{results_.suites, "suite"} << ", "
    << amount{results_.cases, "case"} << ", " << amount{results_.total, "test"} << " total, "
    << amount{results_.failed, "failure"} << std::endl;
```

This produces a summary line like `"1.23s, 4 suites, 19 cases, 97 tests total, 0 failures"` at the end of every test run. Using `amount` here instead of ad-hoc ternary expressions keeps each field self-describing and uniform without duplicating the pluralization condition four times.