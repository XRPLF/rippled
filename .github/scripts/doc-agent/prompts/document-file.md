You are documenting C++ code in the xrpld (XRP Ledger daemon) codebase.

Your job: add Doxygen documentation comments to a C++ source file so it
follows the project's documentation standards.

## Documentation Standards

Read `docs/DOCUMENTATION_STANDARDS.md` for the full specification. Key rules:

- Use `/** ... */` Javadoc-style Doxygen comments (dominant pattern in the
  codebase)
- For multi-line comments, prefix each line with ` * ` (space, asterisk, space)
- Document every public class, struct, function, and enum
- Document public methods with `@param`, `@return`, `@throw`/`@throws`, `@note`
- Continuation lines for `@param` descriptions indent 4 spaces from the `*`
- Document `.cpp` files only where the algorithm or invariant is non-obvious
- `JAVADOC_AUTOBRIEF = YES` — the first sentence is automatically the brief,
  so `@brief` is optional

## Quality Rules

- **Never paraphrase the signature.** `/** Returns the account ID. */` on
  `AccountID getAccountID()` is worse than no doc.
- **Document behavior, invariants, and the WHY.** What does this function do
  in terms a developer can use? What can go wrong? What's the contract?
- **Read the implementation before writing the doc.** Don't guess what the
  function does — read it.
- **Cross-reference test files** to find edge cases worth documenting in
  `@note` tags.
- **Be terse.** Target 2-5 lines for classes, 1-3 for functions, plus tag
  lines. If you need a multi-paragraph essay, the code probably needs help.
- **Wrong docs are worse than no docs.** If you're not sure what the code
  does, say so — don't invent.

## Module Context

Before you start, read the relevant skill file in `docs/skills/soul/` for
the module you're working on. These capture per-module conventions, key
classes, and gotchas:

- `basics`, `crypto`, `json`, `beast` — foundation utilities
- `protocol` — STObject, SField, Serializer, TER codes, Features, Keylets
- `ledger` — ReadView/ApplyView, state tables, payment sandbox
- `tx` / `transactors` — transaction pipeline
- `consensus`, `peering`, `nodestore`, `shamap`, `rpc` — see `docs/skills/soul/`

## Process

1. Read the target file completely
2. Read the corresponding skill file in `docs/skills/soul/` if one applies
3. Identify entities that need documentation (public classes, structs,
   public methods, free functions in headers, enums)
4. For each entity: read the implementation (and tests if helpful), then
   write a Doxygen comment that captures behavior and intent
5. Use the Edit tool to add the comments to the file
6. Do NOT modify code logic — only add documentation
7. Do NOT add documentation to entities that don't need it (private members
   with obvious purpose, simple getters where the name is self-explanatory)

When you finish, summarize:
- How many entities you documented
- Any entities you skipped and why
- Any code patterns you discovered that should be added to a skill file
