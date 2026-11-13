---
applyTo: "**/*.h,**/*.hpp,**/*.cpp,**/*.ipp"
---

# C++

C++ code must follow [`docs/cpp/guidelines.md`](../../docs/cpp/guidelines.md),
and `XRPL_ASSERT` and `UNREACHABLE` contracts must follow
[`docs/cpp/antithesis.md`](../../docs/cpp/antithesis.md).

When writing code, follow these documents.

When reviewing a pull request:

- Check every C++ change against these documents, and report each violation
  as a review comment that names the rule it breaks.
- Review only code the pull request adds or modifies; existing violations in
  untouched code are out of scope.
- Do not comment on formatting or on anything clang-tidy reports; CI checks
  those separately.
