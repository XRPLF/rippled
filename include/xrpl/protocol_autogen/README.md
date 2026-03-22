# Protocol Autogen

This directory contains auto-generated C++ wrapper classes for XRP Ledger protocol types.

## Generated Files

The files in this directory are automatically generated at **CMake configure time** from macro definition files:

- **Transaction classes** (in `transactions/`): Generated from `include/xrpl/protocol/detail/transactions.macro` by `scripts/generate_tx_classes.py`
- **Ledger entry classes** (in `ledger_entries/`): Generated from `include/xrpl/protocol/detail/ledger_entries.macro` by `scripts/generate_ledger_classes.py`

## Generation Process

The generation happens automatically when you **configure** the project (not during build). When you run CMake, the system:

1. Creates a Python virtual environment in the build directory (`codegen_venv`)
2. Installs Python dependencies from `scripts/requirements.txt` into the venv (only if needed)
3. Runs the Python generation scripts using the venv Python interpreter
4. Parses the macro files to extract type definitions
5. Generates type-safe C++ wrapper classes using Mako templates
6. Places the generated headers in this directory

### When Regeneration Happens

The code is regenerated when:

- You run CMake configure for the first time
- The Python virtual environment doesn't exist
- `scripts/requirements.txt` has been modified

To force regeneration, delete the build directory and reconfigure.

### Python Dependencies

The code generation requires the following Python packages (automatically installed):

- `pcpp` - C preprocessor for Python
- `pyparsing` - Parser combinator library
- `Mako` - Template engine

These are isolated in a virtual environment and won't affect your system Python installation.

## Version Control

The generated `.h` files **are checked into version control**. This means:

- Developers without Python 3 can still build the project using the committed files
- CI/CD systems don't need to run code generation if files are up to date
- Changes to generated files are visible in code review

## Modifying Generated Code

**Do not manually edit generated files.** Any changes will be overwritten the next time CMake configure runs.

To modify the generated classes:

- Edit the macro files in `include/xrpl/protocol/detail/`
- Edit the Mako templates in `scripts/templates/`
- Edit the generation scripts in `scripts/`
- Update Python dependencies in `scripts/requirements.txt`
- Run CMake configure to regenerate

## Adding Common Fields

If you add a new common field to `TxFormats.cpp` or `LedgerFormats.cpp`, you should also update the corresponding base classes and templates manually:

Base classes:

- `TransactionBase.h` - Add getters for new common transaction fields
- `TransactionBuilderBase.h` - Add setters, and if the field is required, add it to the constructor parameters
- `LedgerEntryBase.h` - Add getters for new common ledger entry fields
- `LedgerEntryBuilderBase.h` - Add setters, and if the field is required, add it to the constructor parameters

Templates (update to pass required common fields to base class constructors):

- `scripts/templates/Transaction.h.mako`
- `scripts/templates/LedgerEntry.h.mako`

These files are **not auto-generated** and must be updated by hand.
