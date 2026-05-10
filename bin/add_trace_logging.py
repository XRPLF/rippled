#!/usr/bin/env python3
"""
Instrument every function in the rippled codebase with TRACE_FUNC().

Adds an RAII trace scope to every function body that logs:
  - ENTER on function entry (with file:line)
  - EXIT on function exit (with file:line and duration in microseconds)

Usage:
    python3 bin/add_trace_logging.py [--dry-run] [--verbose] [--dir path]
"""

import argparse
import os
import re
import sys
from pathlib import Path

TRACE_INCLUDE = '#include <xrpl/basics/TraceLog.h>'
TRACE_CALL = '    TRACE_FUNC();'

# Control-flow keywords that produce a '{' on its own line
# (due to AfterControlStatement: true in .clang-format)
CONTROL_KW_RE = re.compile(
    r'^\s*('
    r'if\s*\(|'
    r'else\s*if\s*\(|'
    r'else\s*$|'
    r'for\s*\(|'
    r'while\s*\(|'
    r'do\s*$|'
    r'switch\s*\(|'
    r'try\s*$|'
    r'catch\s*\('
    r')'
)

# Structural keywords (class/struct/enum/union) — brace on own line
STRUCT_KW_RE = re.compile(
    r'^\s*(class|struct|enum|union)\b'
)

# Namespace — AfterNamespace: false so '{' is on same line,
# but handle edge cases where someone puts it on next line
NAMESPACE_RE = re.compile(r'^\s*namespace\b')

# Lambda capture: line contains ] possibly followed by () and qualifiers
LAMBDA_RE = re.compile(r'\]\s*(\([^)]*\))?\s*(mutable\s*)?(noexcept\s*)?'
                        r'(->[\s\S]*)?\s*$')

# Function ending: line ends with ) and optional qualifiers
FUNC_END_RE = re.compile(
    r'\)\s*'
    r'(const\s*)?'
    r'(volatile\s*)?'
    r'(noexcept(\([^)]*\))?\s*)?'
    r'(override\s*)?'
    r'(final\s*)?'
    r'(requires\s*\([^)]*\)\s*)?'
    r'(->[\s\w:<>,*&]+\s*)?'
    r'\s*$'
)

# Constructor initializer list line: starts with : or , for member init
INIT_LIST_RE = re.compile(r'^\s*[,:]\s+\w')

# Macro-like patterns to skip
MACRO_RE = re.compile(r'^\s*\\?\s*$')


def find_source_files(root, dirs, extensions=('.cpp',), exclude_dirs=None):
    """Find all source files under given directories."""
    if exclude_dirs is None:
        exclude_dirs = {'tests', 'test'}

    files = []
    for d in dirs:
        search_dir = root / d
        if not search_dir.exists():
            continue
        for ext in extensions:
            for f in sorted(search_dir.rglob(f'*{ext}')):
                # Skip test directories
                parts = f.relative_to(root).parts
                if any(p in exclude_dirs for p in parts):
                    continue
                files.append(f)
    return files


def find_template_headers(root):
    """Find .h files with template implementations that need instrumentation."""
    headers = []
    # These directories have header-only template implementations
    template_dirs = [
        'src/xrpld/consensus',
        'src/xrpld/overlay',
        'src/xrpld/overlay/detail',
        'src/xrpld/peerfinder',
        'src/xrpld/peerfinder/detail',
        'src/xrpld/app/consensus',
        'src/xrpld/app/ledger',
        'src/xrpld/app/ledger/detail',
        'src/xrpld/app/misc',
        'src/xrpld/app/misc/detail',
        'src/xrpld/app/main',
        'src/xrpld/rpc',
        'src/xrpld/rpc/detail',
        'src/xrpld/core',
        'src/xrpld/core/detail',
    ]
    for d in template_dirs:
        search_dir = root / d
        if not search_dir.exists():
            continue
        for f in sorted(search_dir.glob('*.h')):
            headers.append(f)

    # Also include key libxrpl headers with implementations
    libxrpl_dirs = [
        'include/xrpl/shamap',
        'include/xrpl/basics',
        'include/xrpl/protocol',
    ]
    for d in libxrpl_dirs:
        search_dir = root / d
        if not search_dir.exists():
            continue
        for f in sorted(search_dir.glob('*.h')):
            # Skip TraceLog.h itself
            if f.name == 'TraceLog.h':
                continue
            headers.append(f)

    return headers


def add_include(lines):
    """Add the TraceLog.h include if not already present. Returns modified lines."""
    # Check if already included
    for line in lines:
        if 'TraceLog.h' in line:
            return lines, False

    # Find insertion point: after last #include <xrpl/...> or <xrpld/...>
    last_xrpl_idx = -1
    last_xrpld_idx = -1
    last_include_idx = -1

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('#include'):
            last_include_idx = i
            if '<xrpl/' in stripped:
                last_xrpl_idx = i
            elif '<xrpld/' in stripped:
                last_xrpld_idx = i

    if last_include_idx == -1:
        return lines, False  # No includes at all — skip

    # Prefer inserting after xrpl/ includes (same group), else after xrpld/
    if last_xrpl_idx >= 0:
        insert_at = last_xrpl_idx + 1
    elif last_xrpld_idx >= 0:
        # Insert after the xrpld block, with a blank line before xrpl includes
        insert_at = last_xrpld_idx + 1
    else:
        insert_at = last_include_idx + 1

    lines.insert(insert_at, TRACE_INCLUDE + '\n')
    return lines, True


def get_prev_nonblank(lines, idx):
    """Get the index of the previous non-blank line before idx."""
    i = idx - 1
    while i >= 0:
        if lines[i].strip():
            return i
        i -= 1
    return -1


def find_matching_open_paren_line(lines, start_idx):
    """
    Starting from start_idx, walk backwards through lines to find the line
    containing the '(' that matches the last ')' on start_idx's line.
    Returns the line index, or -1 if not found.
    Uses character-level paren tracking for accuracy.
    """
    depth = 0
    for scan in range(start_idx, max(start_idx - 30, -1), -1):
        line = lines[scan].rstrip()
        for ch in reversed(line):
            if ch == ')':
                depth += 1
            elif ch == '(':
                depth -= 1
                if depth == 0:
                    return scan
    return -1


def is_lambda(lines, brace_idx):
    """Check if the '{' at brace_idx opens a lambda body."""
    # Walk back up to 5 non-blank lines looking for ] (lambda capture close)
    prev = brace_idx - 1
    checked = 0
    while prev >= 0 and checked < 6:
        line = lines[prev].rstrip()
        if not line.strip():
            prev -= 1
            continue
        # Lambda pattern: line contains ']' possibly followed by () qualifiers
        if ']' in line:
            # Skip C++ attributes like [[nodiscard]], [[maybe_unused]], etc.
            # Attributes use [[ ]] (double brackets)
            stripped = line.strip()
            if re.match(r'^\[\[', stripped):
                # This is a C++ attribute, not a lambda
                checked += 1
                prev -= 1
                continue
            # Also skip ]] patterns mid-line (attributes in return types)
            if ']]' in line and '[[' in line:
                checked += 1
                prev -= 1
                continue

            # Check for actual lambda pattern: ] followed by ( or { or mutable
            # Lambda captures end with ] then optional (params) or {body}
            if re.search(r'\]\s*(\([^)]*\))?\s*(mutable\s*)?(noexcept\s*)?'
                         r'(->[\s\S]*)?\s*$', line):
                return True
        # Stop walking back at scope boundaries
        if line.strip().startswith('{') or line.strip().startswith('}'):
            break
        if line.strip().startswith('#'):
            break
        checked += 1
        prev -= 1
    return False


def is_function_body(lines, brace_idx):
    """
    Determine if the '{' at brace_idx opens a function body.

    Returns True if this looks like a function definition, False otherwise.
    """
    prev_idx = get_prev_nonblank(lines, brace_idx)
    if prev_idx < 0:
        return False

    prev_line = lines[prev_idx].rstrip()

    # Skip if preceded by a single-line control keyword
    if CONTROL_KW_RE.match(prev_line):
        return False

    # Skip namespace
    if NAMESPACE_RE.match(prev_line):
        return False

    # Skip class/struct/enum/union
    if STRUCT_KW_RE.match(prev_line):
        return False

    # Skip lambdas
    if is_lambda(lines, brace_idx):
        return False

    # Case 1: Previous line ends with ) + optional qualifiers
    if FUNC_END_RE.search(prev_line):
        # Walk back to find the line with the matching '('
        open_paren_line = find_matching_open_paren_line(lines, prev_idx)
        if open_paren_line >= 0:
            # Check if that line (or preceding lines) is a control keyword
            line = lines[open_paren_line].rstrip()
            if CONTROL_KW_RE.match(line):
                return False
            # Also check the line before (for multi-line: "else\n if(...)")
            prev_kw = get_prev_nonblank(lines, open_paren_line)
            if prev_kw >= 0:
                kw_line = lines[prev_kw].rstrip()
                if re.match(r'^\s*else\s*$', kw_line):
                    return False
        return True

    # Case 2: Constructor initializer list — line starts with , or :
    if INIT_LIST_RE.match(prev_line):
        # Walk back through initializer list to find constructor signature
        scan = prev_idx - 1
        while scan >= 0:
            line = lines[scan].rstrip()
            if FUNC_END_RE.search(line):
                return True
            if not INIT_LIST_RE.match(line) and ')' not in line:
                break
            scan -= 1
        # Even if we can't find the signature, an init list implies constructor
        return True

    # Case 3: Previous line ends with something unusual — check a few
    # lines back for a ')' that might be part of a function signature
    # (e.g., trailing requires clause or -> return type on separate line)
    for lookback in range(1, 4):
        check_idx = prev_idx - lookback
        if check_idx < 0:
            break
        check_line = lines[check_idx].rstrip()
        if check_line.strip() == '' or check_line.strip().startswith('#'):
            break
        if FUNC_END_RE.search(check_line):
            open_paren_line = find_matching_open_paren_line(
                lines, check_idx)
            if open_paren_line >= 0:
                if CONTROL_KW_RE.match(lines[open_paren_line].rstrip()):
                    return False
            return True

    return False


def is_empty_body(lines, brace_idx):
    """Check if the function body is empty (next non-blank line is '}')."""
    i = brace_idx + 1
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped:
            return stripped == '}'
        i += 1
    return True


def already_has_trace(lines, brace_idx):
    """Check if TRACE_FUNC is already the first statement."""
    i = brace_idx + 1
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped:
            return 'TRACE_FUNC()' in stripped
        i += 1
    return False


def get_function_context(lines, brace_idx):
    """Try to extract a meaningful function name from the context."""
    prev_idx = get_prev_nonblank(lines, brace_idx)
    if prev_idx < 0:
        return "unknown"

    # Walk back to find the function name line
    scan = prev_idx
    while scan >= 0:
        line = lines[scan].strip()
        # Look for ClassName::methodName or just functionName
        match = re.search(r'(\w+(?:::\w+)?)\s*\(', line)
        if match:
            return match.group(1)
        if line.startswith('#') or line == '':
            break
        scan -= 1

    return lines[prev_idx].strip()[:60]


def instrument_file(filepath, dry_run=False, verbose=False):
    """
    Add TRACE_FUNC() to every function body in the given file.
    Returns (functions_found, include_added) tuple.
    """
    with open(filepath, 'r') as f:
        lines = f.readlines()

    original_lines = list(lines)
    functions_found = 0
    insertions = []  # (line_index, context_string)

    # Pass 1: Find all function body openings
    # Track brace depth to skip nested blocks inside functions
    brace_depth = 0
    in_function = False
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if in_function:
            # Count braces to track nesting within the current function
            brace_depth += stripped.count('{') - stripped.count('}')
            if brace_depth <= 0:
                in_function = False
                brace_depth = 0
        elif stripped == '{':
            if is_function_body(lines, i):
                if not is_empty_body(lines, i) and not already_has_trace(lines, i):
                    ctx = get_function_context(lines, i)
                    insertions.append((i, ctx))
                    functions_found += 1
                in_function = True
                brace_depth = 1
        i += 1

    if functions_found == 0 and not any('TraceLog.h' in l for l in lines):
        return 0, False  # Nothing to do

    # Pass 2: Add include
    include_added = False
    if functions_found > 0:
        lines, include_added = add_include(lines)
        # Adjust insertion indices if include was added
        if include_added:
            # Find where the include was inserted
            for idx in range(len(lines)):
                if TRACE_INCLUDE in lines[idx]:
                    include_line = idx
                    break
            insertions = [
                (i + 1 if i >= include_line else i, ctx)
                for i, ctx in insertions
            ]

    # Pass 3: Insert TRACE_FUNC() calls (reverse order to preserve indices)
    for insert_idx, ctx in reversed(insertions):
        lines.insert(insert_idx + 1, TRACE_CALL + '\n')

    if verbose and functions_found > 0:
        rel_path = filepath.name
        print(f"  {rel_path}: {functions_found} functions")
        for _, ctx in insertions:
            print(f"    -> {ctx}")

    if not dry_run and lines != original_lines:
        with open(filepath, 'w') as f:
            f.writelines(lines)

    return functions_found, include_added


def main():
    parser = argparse.ArgumentParser(
        description='Add TRACE_FUNC() to every function in the rippled codebase')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print what would change without modifying files')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show each function found')
    parser.add_argument('--dir', type=str, default=None,
                        help='Only process files under this subdirectory')
    parser.add_argument('--headers', action='store_true',
                        help='Also process .h files with template implementations')
    parser.add_argument('--root', type=str, default='.',
                        help='Project root directory')
    args = parser.parse_args()

    root = Path(args.root).resolve()

    # Find source files
    if args.dir:
        dirs = [args.dir]
    else:
        dirs = ['src/xrpld', 'src/libxrpl']

    files = find_source_files(root, dirs)

    if args.headers:
        files.extend(find_template_headers(root))

    if not files:
        print(f"No source files found under {root}")
        sys.exit(1)

    total_functions = 0
    total_includes = 0
    total_files_modified = 0

    mode = "DRY RUN" if args.dry_run else "INSTRUMENTING"
    print(f"[{mode}] Processing {len(files)} files under {root}")
    print()

    for filepath in files:
        funcs, inc = instrument_file(
            filepath, dry_run=args.dry_run, verbose=args.verbose)
        if funcs > 0:
            total_functions += funcs
            total_files_modified += 1
        if inc:
            total_includes += 1

    print()
    print(f"{'Would instrument' if args.dry_run else 'Instrumented'}: "
          f"{total_functions} functions across {total_files_modified} files")
    print(f"{'Would add' if args.dry_run else 'Added'}: "
          f"{total_includes} new #include directives")


if __name__ == '__main__':
    main()
