#!/usr/bin/env python3
"""
Generate C++ wrapper classes for XRP Ledger entry types from ledger_entries.macro.

This script parses the ledger_entries.macro file and generates type-safe wrapper
classes for each ledger entry type, similar to the transaction wrapper classes.

Uses pcpp to preprocess the macro file and pyparsing to parse the DSL.
"""

import io
import argparse
from pathlib import Path
import pyparsing as pp

# Import common utilities
from macro_parser_common import (
    CppCleaner,
    parse_sfields_macro,
    parse_field_list,
    generate_cpp_class,
    generate_from_template,
    clear_output_directory,
)


def create_ledger_entry_parser():
    """Create a pyparsing parser for LEDGER_ENTRY macros.

    This parser extracts the full LEDGER_ENTRY macro call and parses its arguments
    using pyparsing's nesting-aware delimited list parsing.
    """
    # Match the exact words
    ledger_entry = pp.Keyword("LEDGER_ENTRY") | pp.Keyword("LEDGER_ENTRY_DUPLICATE")

    # Define nested structures so pyparsing protects them
    nested_braces = pp.original_text_for(pp.nested_expr("{", "}"))
    nested_parens = pp.original_text_for(pp.nested_expr("(", ")"))

    # Define standard text (anything that isn't a comma, parens, or braces)
    plain_text = pp.Word(pp.printables + " \t\n", exclude_chars=",{}()")

    # A single argument is any combination of the above
    single_arg = pp.Combine(pp.OneOrMore(nested_braces | nested_parens | plain_text))
    single_arg.set_parse_action(lambda t: t[0].strip())

    # The arguments are a delimited list
    args_list = pp.DelimitedList(single_arg)

    # The full macro: LEDGER_ENTRY(args) or LEDGER_ENTRY_DUPLICATE(args)
    macro_parser = (
        ledger_entry + pp.Suppress("(") + pp.Group(args_list)("args") + pp.Suppress(")")
    )

    return macro_parser


def parse_ledger_entry_args(args_list):
    """Parse the arguments of a LEDGER_ENTRY macro call.

    Args:
        args_list: A list of parsed arguments from pyparsing, e.g.,
                   ['ltACCOUNT_ROOT', '0x0061', 'AccountRoot', 'account', '({...})']

    Returns:
        A dict with parsed ledger entry information.
    """
    if len(args_list) < 5:
        raise ValueError(
            f"Expected at least 5 parts in LEDGER_ENTRY, got {len(args_list)}: {args_list}"
        )

    tag = args_list[0]
    value = args_list[1]
    name = args_list[2]
    rpc_name = args_list[3]
    fields_str = args_list[-1]

    # Parse fields: ({field1, field2, ...})
    fields = parse_field_list(fields_str)

    return {
        "tag": tag,
        "value": value,
        "name": name,
        "rpc_name": rpc_name,
        "fields": fields,
    }


def parse_macro_file(file_path):
    """Parse the ledger_entries.macro file and return a list of ledger entry definitions.

    Uses pcpp to preprocess the file and pyparsing to parse the LEDGER_ENTRY macros.
    """
    with open(file_path, "r") as f:
        c_code = f.read()

    # Step 1: Clean the C++ code using pcpp
    cleaner = CppCleaner("LEDGER_ENTRY_INCLUDE", "LEDGER_ENTRY")
    cleaner.parse(c_code)

    out = io.StringIO()
    cleaner.write(out)
    clean_text = out.getvalue()

    # Step 2: Parse the clean text using pyparsing
    parser = create_ledger_entry_parser()
    entries = []

    for match, _, _ in parser.scan_string(clean_text):
        # Extract the macro name and arguments
        raw_args = match.args

        # Parse the arguments
        entry_data = parse_ledger_entry_args(raw_args)
        entries.append(entry_data)

    return entries


def main():
    parser = argparse.ArgumentParser(
        description="Generate C++ ledger entry classes from ledger_entries.macro"
    )
    parser.add_argument("macro_path", help="Path to ledger_entries.macro")
    parser.add_argument(
        "--header-dir",
        help="Output directory for header files",
        default="include/xrpl/protocol_autogen/ledger_entries",
    )
    parser.add_argument(
        "--test-dir",
        help="Output directory for test files (optional)",
        default=None,
    )
    parser.add_argument(
        "--sfields-macro",
        help="Path to sfields.macro (default: auto-detect from macro_path)",
    )
    parser.add_argument(
        "--list-outputs",
        action="store_true",
        help="List output files without generating (one per line)",
    )

    args = parser.parse_args()

    # Parse the macro file to get ledger entry names
    entries = parse_macro_file(args.macro_path)

    # If --list-outputs, just print the output file paths and exit
    if args.list_outputs:
        header_dir = Path(args.header_dir)
        for entry in entries:
            print(header_dir / f"{entry['name']}.h")
        if args.test_dir:
            test_dir = Path(args.test_dir)
            for entry in entries:
                print(test_dir / f"{entry['name']}Tests.cpp")
        return

    # Auto-detect sfields.macro path if not provided
    if args.sfields_macro:
        sfields_path = Path(args.sfields_macro)
    else:
        # Assume sfields.macro is in the same directory as ledger_entries.macro
        macro_path = Path(args.macro_path)
        sfields_path = macro_path.parent / "sfields.macro"

    # Parse sfields.macro to get field type information
    print(f"Parsing {sfields_path}...")
    field_types = parse_sfields_macro(sfields_path)
    print(
        f"Found {len(field_types)} field definitions ({sum(1 for f in field_types.values() if f['typed'])} typed, {sum(1 for f in field_types.values() if not f['typed'])} untyped)\n"
    )

    print(f"Found {len(entries)} ledger entries\n")

    for entry in entries:
        print(f"Ledger Entry: {entry['name']}")
        print(f"  Tag: {entry['tag']}")
        print(f"  Value: {entry['value']}")
        print(f"  RPC Name: {entry['rpc_name']}")
        print(f"  Fields: {len(entry['fields'])}")
        for field in entry["fields"]:
            mpt_info = f" ({field['mpt_support']})" if "mpt_support" in field else ""
            print(f"    - {field['name']}: {field['requirement']}{mpt_info}")
        print()

    # Set up template directory
    script_dir = Path(__file__).parent
    template_dir = script_dir / "templates"

    # Generate C++ classes
    header_dir = Path(args.header_dir)
    header_dir.mkdir(parents=True, exist_ok=True)

    # Clear existing generated files before regenerating
    clear_output_directory(header_dir)

    for entry in entries:
        generate_cpp_class(
            entry, header_dir, template_dir, field_types, "LedgerEntry.h.mako"
        )

    print(f"\nGenerated {len(entries)} ledger entry classes")

    # Generate unit tests if --test-dir is provided
    if args.test_dir:
        test_dir = Path(args.test_dir)
        test_dir.mkdir(parents=True, exist_ok=True)

        # Clear existing generated test files before regenerating
        clear_output_directory(test_dir)

        for entry in entries:
            # Fields are already enriched from generate_cpp_class above
            generate_from_template(
                entry, test_dir, template_dir, "LedgerEntryTests.cpp.mako", "Tests.cpp"
            )

        print(f"\nGenerated {len(entries)} ledger entry test files")


if __name__ == "__main__":
    main()
