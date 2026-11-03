# Code review guide

This guide collects the 18 most critical review rules that come up on pull requests in this repository. It is written for both human reviewers and AI reviewers, and for authors who want to self-review before asking for a review. The rules were distilled from maintainer review comments on public `rippled` pull requests; each rule links to one or more example comments.

It complements, and does not replace, [CONTRIBUTING.md](../CONTRIBUTING.md) (process, contracts, tests, style guide) and [docs/CodingStyle.md](./CodingStyle.md) (formatting). Where a rule overlaps those documents, it links to the relevant section.

## How to use

- Apply the **Correctness** rules first. Ignoring one risks wrong ledger state, a wrong result, a security issue, or a bug reaching a release.
- Then apply the **Style and conventions** rules.
- Cite the rule number in a review comment, e.g. "CR-17", so the author can find the rule and its examples.
- Rules are numbered continuously through the document so that a number is stable and citable; the headings only group them by theme.
- A rule is a default, not a law: if a change deliberately departs from one, the PR should say why.

## Correctness

### Amendments and feature gating

- **CR-1.** Respect amendment state until formal retirement: always check it as if disabled, preserve pre-amendment behaviour even when wrong, honour pre-amendment objects, and when retiring remove only the gating path. — [#6381](https://github.com/XRPLF/rippled/pull/6381#discussion_r2914804447) [#5650](https://github.com/XRPLF/rippled/pull/5650#discussion_r2294615413) [#6815](https://github.com/XRPLF/rippled/pull/6815#discussion_r3081221668)

### Transactor structure and result codes

- **CR-2.** Put checks needing only transaction fields and Rules in preflight (tem codes); ledger-dependent or mutating checks go in preclaim or doApply; run universal preflight checks before transactor-specific ones. — [#5187](https://github.com/XRPLF/rippled/pull/5187#discussion_r1844620179) [#5354](https://github.com/XRPLF/rippled/pull/5354#discussion_r2010888968) [#5650](https://github.com/XRPLF/rippled/pull/5650#discussion_r2294652160)
- **CR-3.** Never throw on transaction data; return tecINTERNAL or tefINTERNAL for conditions earlier steps made impossible, keep logicError in destructors and consensus paths, and catch every throwing call. — [#6446](https://github.com/XRPLF/rippled/pull/6446#discussion_r3471187820) [#5652](https://github.com/XRPLF/rippled/pull/5652#discussion_r2285932675)
- **CR-4.** Return the result code that describes the actual failure, propagate a helper's TER unchanged, tolerate only the specific expected error, and never let cleanup failures override the primary result. — [#5652](https://github.com/XRPLF/rippled/pull/5652#discussion_r2288255721) [#5224](https://github.com/XRPLF/rippled/pull/5224#discussion_r1931389839) [#5863](https://github.com/XRPLF/rippled/pull/5863#discussion_r2453585694)
- **CR-5.** Keep every transactor check and result code in sync with the XLS spec; when the spec omits a protocol invariant or is wrong, fix the spec rather than silently diverging. — [#5270](https://github.com/XRPLF/rippled/pull/5270#discussion_r2326189564) [#6361](https://github.com/XRPLF/rippled/pull/6361#discussion_r2849902387) [#6133](https://github.com/XRPLF/rippled/pull/6133#discussion_r2625218980)

### Ledger objects, fields and flags

- **CR-6.** Null-check every SLE from view.read or peek before dereferencing and return tefBAD_LEDGER or tefINTERNAL with LCOV_EXCL_LINE, even when a null is nearly impossible. — [#7886](https://github.com/XRPLF/rippled/pull/7886#discussion_r3799843692) [#5270](https://github.com/XRPLF/rippled/pull/5270#discussion_r2558234275) [#6715](https://github.com/XRPLF/rippled/pull/6715#discussion_r3096935376)
- **CR-7.** Call view.update(sle) once per modified SLE per transaction; its position among the modifications is irrelevant, but it must happen. — [#5270](https://github.com/XRPLF/rippled/pull/5270#discussion_r2389606850) [#6408](https://github.com/XRPLF/rippled/pull/6408#discussion_r2865782440)

### Invariants, asserts and defensive checks

- **CR-8.** For impossible branches write `if (cond) { UNREACHABLE(...); return error; }` with LCOV exclusion; a bare XRPL_ASSERT or a silent tefINTERNAL alone is insufficient. See also [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation). — [#5224](https://github.com/XRPLF/rippled/pull/5224#discussion_r1932980450) [#7354](https://github.com/XRPLF/rippled/pull/7354#discussion_r3325640961) [#7445](https://github.com/XRPLF/rippled/pull/7445#discussion_r3470744246)
- **CR-9.** Invariants must hold when the transaction fails: never return true early on a non-tesSUCCESS result; instead verify no guarded objects were touched. — [#6134](https://github.com/XRPLF/rippled/pull/6134#discussion_r2641358394) [#6477](https://github.com/XRPLF/rippled/pull/6477#discussion_r2908054264)

### Tests

- **CR-10.** After each transaction in a test, assert the resulting ledger state and balances in every branch, including absent objects, flags and transfer-fee effects, not just the result code. — [#5143](https://github.com/XRPLF/rippled/pull/5143#discussion_r1806992341) [#6108](https://github.com/XRPLF/rippled/pull/6108#discussion_r2608368033) [#5997](https://github.com/XRPLF/rippled/pull/5997#discussion_r2495065648)
- **CR-11.** Validate the tests themselves: confirm a regression test fails without the fix, question unexplained expectation changes, and check nothing silently disables a test or fails for the wrong reason. — [#5270](https://github.com/XRPLF/rippled/pull/5270#discussion_r2393271064) [#5670](https://github.com/XRPLF/rippled/pull/5670#discussion_r2302084434) [#6113](https://github.com/XRPLF/rippled/pull/6113#discussion_r2641453312)

### Concurrency and locking

- **CR-12.** Never allocate or copy large containers while holding a lock; reserve outside the lock, re-check under it, or hand out shared_ptr snapshots. — [#7567](https://github.com/XRPLF/rippled/pull/7567#discussion_r3582093861) [#8240](https://github.com/XRPLF/rippled/pull/8240#discussion_r4189885137) [#8540](https://github.com/XRPLF/rippled/pull/8540#discussion_r4252532717)

### Peer protocol and networking

- **CR-13.** Gate peer-protocol behaviour changes on a new ProtocolVersion, keeping old behaviour for older peers. — [#6110](https://github.com/XRPLF/rippled/pull/6110#discussion_r2714233863)

## Style and conventions

### Code style and idioms

- **CR-14.** Declare locals const (east style `auto const`), initialised in one statement from the producing expression, value_or or a conditional, and bind aliases and structured bindings with auto const&. — [#5652](https://github.com/XRPLF/rippled/pull/5652#discussion_r2285873480) [#5143](https://github.com/XRPLF/rippled/pull/5143#discussion_r1806698254) [#5302](https://github.com/XRPLF/rippled/pull/5302#discussion_r1968473110)
- **CR-15.** Prefer early returns over else branches and nested ifs; hoist disqualifying conditions into one early return and remove checks made redundant upstream. — [#5243](https://github.com/XRPLF/rippled/pull/5243#discussion_r1929272856) [#5187](https://github.com/XRPLF/rippled/pull/5187#discussion_r1844643325) [#5857](https://github.com/XRPLF/rippled/pull/5857#discussion_r2417751211)

### Structure and reuse

- **CR-16.** Put guards, degenerate cases and amendment gates inside the helper or virtual hook so callers cannot forget them, and never re-check logic the helper already performs. — [#5187](https://github.com/XRPLF/rippled/pull/5187#discussion_r1929057804) [#5270](https://github.com/XRPLF/rippled/pull/5270#discussion_r2553757992) [#5354](https://github.com/XRPLF/rippled/pull/5354#discussion_r2003586428)

### Ledger and transactor idioms

- **CR-17.** Prefer an existing specific result code (temINVALID_FLAG, tecPATH_DRY, tecNO_PERMISSION) over redundant temMALFORMED or a new tec; the code space is limited. — [#5224](https://github.com/XRPLF/rippled/pull/5224#discussion_r1929394690) [#5201](https://github.com/XRPLF/rippled/pull/5201#discussion_r1854799275)

### Test conventions

- **CR-18.** In unit tests and jtx helpers never use assert or XRPL_ASSERT; write `if (!BEAST_EXPECT(x)) return;` or Throw, give switches a failing default, and prefer BEAST_EXPECT over expect. See also [CONTRIBUTING.md](../CONTRIBUTING.md#contracts-and-instrumentation) (where plain `assert` is still expected). — [#5143](https://github.com/XRPLF/rippled/pull/5143#discussion_r1802124743) [#5004](https://github.com/XRPLF/rippled/pull/5004#discussion_r1831731533) [#6117](https://github.com/XRPLF/rippled/pull/6117#discussion_r2615991651)
