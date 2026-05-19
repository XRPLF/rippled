#!/usr/bin/env python3
"""
Common utilities for parsing XRP Ledger macro files.

This module provides shared functionality for parsing transactions.macro
and ledger_entries.macro files using pcpp and pyparsing.
"""

import re
import shutil
from pathlib import Path
import pyparsing as pp
from pcpp import Preprocessor


def clear_output_directory(directory):
    """Clear all generated files from an output directory.

    Removes all .h and .cpp files from the directory, but preserves
    the directory itself and any subdirectories.

    Args:
        directory: Path to the directory to clear
    """
    dir_path = Path(directory)
    if not dir_path.exists():
        return

    # Remove generated files (headers and source files)
    for pattern in ["*.h", "*.cpp"]:
        for file_path in dir_path.glob(pattern):
            file_path.unlink()

    print(f"Cleared output directory: {dir_path}")


class CppCleaner(Preprocessor):
    """C preprocessor that removes C++ noise while preserving macro calls."""

    def __init__(self, macro_include_name, macro_name):
        """
        Initialize the preprocessor.

        Args:
            macro_include_name: The name of the include flag to set to 0
                               (e.g., "TRANSACTION_INCLUDE" or "LEDGER_ENTRY_INCLUDE")
            macro_name: The name of the macro to define so #if !defined() checks pass
                       (e.g., "TRANSACTION" or "LEDGER_ENTRY")
        """
        super(CppCleaner, self).__init__()
        # Define flags so #if blocks evaluate correctly
        # We set the include flag to 0 so includes are skipped
        self.define(f"{macro_include_name} 0")
        # Define the macro so #if !defined(MACRO) / #error checks pass
        # We define it to expand to itself so the macro calls remain in the output
        # for pyparsing to find and parse
        self.define(f"{macro_name}(...) {macro_name}(__VA_ARGS__)")
        # Suppress line directives
        self.line_directive = None

    def on_error(self, file, line, msg):
        # Ignore #error directives
        pass

    def on_include_not_found(
        self, is_malformed, is_system_include, curdir, includepath
    ):
        # Ignore missing headers
        pass


def parse_sfields_macro(sfields_path):
    """
    Parse sfields.macro to determine which fields are typed vs untyped.

    Returns a dict mapping field names to their type information:
    {
        'sfMemos': {'typed': False, 'stiSuffix': 'ARRAY', 'typeData': {...}},
        'sfAmount': {'typed': True, 'stiSuffix': 'AMOUNT', 'typeData': {...}},
        ...
    }
    """
    # Mapping from STI suffix to C++ type for untyped fields
    UNTYPED_TYPE_MAP = {
        "ARRAY": {
            "getter_method": "getFieldArray",
            "setter_method": "setFieldArray",
            "setter_use_brackets": False,
            "setter_type": "STArray const&",
            "return_type": "STArray const&",
            "return_type_optional": "std::optional<std::reference_wrapper<STArray const>>",
        },
        "OBJECT": {
            "getter_method": "getFieldObject",
            "setter_method": "setFieldObject",
            "setter_use_brackets": False,
            "setter_type": "STObject const&",
            "return_type": "STObject",
            "return_type_optional": "std::optional<STObject>",
        },
        "PATHSET": {
            "getter_method": "getFieldPathSet",
            "setter_method": "setFieldPathSet",
            "setter_use_brackets": False,
            "setter_type": "STPathSet const&",
            "return_type": "STPathSet const&",
            "return_type_optional": "std::optional<std::reference_wrapper<STPathSet const>>",
        },
    }

    field_info = {}

    with open(sfields_path, "r") as f:
        content = f.read()

    # Parse TYPED_SFIELD entries
    # Format: TYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)
    typed_pattern = r"TYPED_SFIELD\s*\(\s*(\w+)\s*,\s*(\w+)\s*,"
    for match in re.finditer(typed_pattern, content):
        field_name = match.group(1)
        sti_suffix = match.group(2)
        field_info[field_name] = {
            "typed": True,
            "stiSuffix": sti_suffix,
            "typeData": {
                "getter_method": "at",
                "setter_method": "",
                "setter_use_brackets": True,
                "setter_type": f"std::decay_t<typename SF_{sti_suffix}::type::value_type> const&",
                "return_type": f"SF_{sti_suffix}::type::value_type",
                "return_type_optional": f"protocol_autogen::Optional<SF_{sti_suffix}::type::value_type>",
            },
        }

    # Parse UNTYPED_SFIELD entries
    # Format: UNTYPED_SFIELD(sfName, stiSuffix, fieldValue, ...)
    untyped_pattern = r"UNTYPED_SFIELD\s*\(\s*(\w+)\s*,\s*(\w+)\s*,"
    for match in re.finditer(untyped_pattern, content):
        field_name = match.group(1)
        sti_suffix = match.group(2)
        type_data = UNTYPED_TYPE_MAP.get(
            sti_suffix, UNTYPED_TYPE_MAP.get("OBJECT")
        )  # Default to OBJECT
        field_info[field_name] = {
            "typed": False,
            "stiSuffix": sti_suffix,
            "typeData": type_data,
        }

    return field_info


def create_field_list_parser():
    """Create a pyparsing parser for field lists like '({...})'."""
    # A field identifier (e.g., sfDestination, soeREQUIRED, soeMPTSupported)
    field_identifier = pp.Word(pp.alphas + "_", pp.alphanums + "_")

    # A single field definition: {sfName, soeREQUIRED, ...}
    # Allow optional trailing comma inside the braces
    field_def = (
        pp.Suppress("{")
        + pp.Group(pp.DelimitedList(field_identifier) + pp.Optional(pp.Suppress(",")))(
            "parts"
        )
        + pp.Suppress("}")
    )

    # The field list: ({field1, field2, ...}) or ({}) for empty lists
    # Allow optional trailing comma after the last field definition
    field_list = (
        pp.Suppress("(")
        + pp.Suppress("{")
        + pp.Group(
            pp.Optional(pp.DelimitedList(field_def) + pp.Optional(pp.Suppress(",")))
        )("fields")
        + pp.Suppress("}")
        + pp.Suppress(")")
    )

    return field_list


def parse_field_list(fields_str):
    """Parse a field list string like '({...})' using pyparsing.

    Args:
        fields_str: A string like '({
            {sfDestination, soeREQUIRED},
            {sfAmount, soeREQUIRED, soeMPTSupported}
        })'

    Returns:
        A list of field dicts with 'name', 'requirement', 'flags', and 'supports_mpt'.
    """
    parser = create_field_list_parser()

    try:
        result = parser.parse_string(fields_str, parse_all=True)
        fields = []

        for field_parts in result.fields:
            if len(field_parts) < 2:
                continue

            field_name = field_parts[0]
            requirement = field_parts[1]
            flags = list(field_parts[2:]) if len(field_parts) > 2 else []
            supports_mpt = "soeMPTSupported" in flags

            fields.append(
                {
                    "name": field_name,
                    "requirement": requirement,
                    "flags": flags,
                    "supports_mpt": supports_mpt,
                }
            )

        return fields
    except pp.ParseException as e:
        raise ValueError(f"Failed to parse field list: {e}")


def enrich_fields_with_type_data(entry_info, field_types):
    """Enrich field information with type data from sfields.macro.

    Args:
        entry_info: Dict containing entry information (name, fields, etc.)
        field_types: Dict mapping field names to type information

    Modifies entry_info["fields"] in place.
    """
    for field in entry_info["fields"]:
        field_name = field["name"]
        if field_name in field_types:
            field["typed"] = field_types[field_name]["typed"]
            field["paramName"] = field_name[2].lower() + field_name[3:]
            field["stiSuffix"] = field_types[field_name]["stiSuffix"]
            field["typeData"] = field_types[field_name]["typeData"]
        else:
            # Unknown field - assume typed for safety
            field["typed"] = True
            field["paramName"] = ""
            field["stiSuffix"] = None
            field["typeData"] = None


def generate_from_template(
    entry_info, output_dir, template_dir, template_name, output_suffix
):
    """Generate a file from a Mako template.

    Args:
        entry_info: Dict containing entry information (name, fields, etc.)
                    Fields should already be enriched with type data.
        output_dir: Output directory for generated files
        template_dir: Directory containing Mako templates
        template_name: Name of the Mako template file to use
        output_suffix: Suffix for the output file (e.g., ".h" or "Tests.cpp")

    Returns:
        Path to the generated file
    """
    from mako.template import Template

    template_path = Path(template_dir) / template_name
    template = Template(filename=str(template_path))

    # Render the template - pass entry_info directly so templates can access any field
    content = template.render(**entry_info)

    # Write output file in binary mode to avoid any line ending conversion
    output_path = Path(output_dir) / f"{entry_info['name']}{output_suffix}"
    with open(output_path, "wb") as f:
        f.write(content.encode("utf-8"))

    print(f"Generated {output_path}")
    return output_path


def generate_cpp_class(
    entry_info, header_dir, template_dir, field_types, template_name
):
    """Generate C++ header file from a Mako template.

    Args:
        entry_info: Dict containing entry information (name, fields, etc.)
        header_dir: Output directory for generated header files
        template_dir: Directory containing Mako templates
        field_types: Dict mapping field names to type information
        template_name: Name of the Mako template file to use
    """
    # Enrich field information with type data
    enrich_fields_with_type_data(entry_info, field_types)

    # Generate the header file
    generate_from_template(entry_info, header_dir, template_dir, template_name, ".h")
