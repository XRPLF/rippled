# Protocol Autogen

This directory contains auto-generated C++ wrapper classes for XRP Ledger protocol types.

## Generated Files

The files in this directory are generated from macro definition files:

- **Transaction classes** (in `transactions/`): Generated from `include/xrpl/protocol/detail/transactions.macro` by `cmake/scripts/codegen/generate_tx_classes.py`
- **Ledger entry classes** (in `ledger_entries/`): Generated from `include/xrpl/protocol/detail/ledger_entries.macro` by `cmake/scripts/codegen/generate_ledger_classes.py`

## Generation Process

Generation requires a one-time setup step to create a virtual environment
and install Python dependencies, followed by running the generation target:

```bash
cmake --build . --target setup_code_gen  # create venv and install dependencies (once)
cmake --build . --target code_gen        # generate code
```

By default, `CODEGEN_VENV_DIR` points to `.venv` in the project root. The
`setup_code_gen` target creates a venv there and installs the required packages.
The `code_gen` target then uses the venv's Python interpreter to run generation.

### Python Dependencies

The code generation requires the following Python packages (installed by `setup_code_gen`):

- `pcpp` - C preprocessor for Python
- `pyparsing` - Parser combinator library
- `Mako` - Template engine

## Version Control

The generated `.h` files **are checked into version control**. This means:

- Developers without Python 3 can still build the project using the committed files
- CI/CD systems don't need to run code generation if files are up to date
- Changes to generated files are visible in code review

## Modifying Generated Code

**Do not manually edit generated files.** Any changes will be overwritten the next time `code_gen` is run.

To modify the generated classes:

- Edit the macro files in `include/xrpl/protocol/detail/`
- Edit the Mako templates in `cmake/scripts/codegen/templates/`
- Edit the generation scripts in `cmake/scripts/codegen/`
- Update Python dependencies in `cmake/scripts/codegen/requirements.txt`
- Run `cmake --build . --target code_gen` to regenerate

## Adding Common Fields

If you add a new common field to `TxFormats.cpp` or `LedgerFormats.cpp`, you should also update the corresponding base classes and templates manually:

Base classes:

- `TransactionBase.h` - Add getters for new common transaction fields
- `TransactionBuilderBase.h` - Add setters, and if the field is required, add it to the constructor parameters
- `LedgerEntryBase.h` - Add getters for new common ledger entry fields
- `LedgerEntryBuilderBase.h` - Add setters, and if the field is required, add it to the constructor parameters

Templates (update to pass required common fields to base class constructors):

- `cmake/scripts/codegen/templates/Transaction.h.mako`
- `cmake/scripts/codegen/templates/LedgerEntry.h.mako`

These files are **not auto-generated** and must be updated by hand.
