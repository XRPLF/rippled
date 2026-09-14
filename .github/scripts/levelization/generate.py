#!/usr/bin/env python3

"""
Usage: generate.py
This script takes no parameters, and can be called from any directory in the file system.
"""

import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple, Set, Optional

# Compile regex patterns once at module level
INCLUDE_PATTERN = re.compile(r"^\s*#include.*/.*\.h")
INCLUDE_PATH_PATTERN = re.compile(r'[<"]([^>"]+)[>"]')


def dictionary_sort_key(s: str) -> str:
    """
    Create a sort key that mimics 'sort -d' (dictionary order).
    Dictionary order only considers blanks and alphanumeric characters.
    This means punctuation like '.' is ignored during sorting.
    """
    # Keep only alphanumeric characters and spaces
    return "".join(c for c in s if c.isalnum() or c.isspace())


def get_level(file_path: str) -> str:
    """
    Extract the level from a file path (second and third directory components).
    Equivalent to bash: cut -d/ -f 2,3

    Examples:
        src/xrpld/app/main.cpp -> xrpld.app
        src/libxrpl/protocol/STObject.cpp -> libxrpl.protocol
        include/xrpl/basics/base_uint.h -> xrpl.basics
    """
    parts = file_path.split("/")

    # Get fields 2 and 3 (indices 1 and 2 in 0-based indexing)
    if len(parts) >= 3:
        level = f"{parts[1]}/{parts[2]}"
    elif len(parts) >= 2:
        level = f"{parts[1]}/toplevel"
    else:
        level = file_path

    # If the "level" indicates a file, cut off the filename
    if "." in level.split("/")[-1]:  # Avoid Path object creation
        # Use the "toplevel" label as a workaround for `sort`
        # inconsistencies between different utility versions
        level = level.rsplit("/", 1)[0] + "/toplevel"

    return level.replace("/", ".")


def extract_include_level(include_line: str) -> Optional[str]:
    """
    Extract the include path from an #include directive.
    Gets the first two directory components from the include path.
    Equivalent to bash: cut -d/ -f 1,2

    Examples:
        #include <xrpl/basics/base_uint.h> -> xrpl.basics
        #include "xrpld/app/main/Application.h" -> xrpld.app
    """
    # Remove everything before the quote or angle bracket
    match = INCLUDE_PATH_PATTERN.search(include_line)
    if not match:
        return None

    include_path = match.group(1)
    parts = include_path.split("/")

    # Get first two fields (indices 0 and 1)
    if len(parts) >= 2:
        include_level = f"{parts[0]}/{parts[1]}"
    else:
        include_level = include_path

    # If the "includelevel" indicates a file, cut off the filename
    if "." in include_level.split("/")[-1]:  # Avoid Path object creation
        include_level = include_level.rsplit("/", 1)[0] + "/toplevel"

    return include_level.replace("/", ".")


def find_repository_directories(
    start_path: Path, depth_limit: int = 10
) -> Tuple[Path, List[Path]]:
    """
    Find the repository root by looking for src or include folders.
    Walks up the directory tree from the start path.
    """
    current = start_path.resolve()

    # Walk up the directory tree
    for _ in range(depth_limit):  # Limit search depth to prevent infinite loops
        src_path = current / "src"
        include_path = current / "include"
        # Check if this directory has src or include folders
        has_src = src_path.exists()
        has_include = include_path.exists()

        if has_src or has_include:
            return current, [src_path, include_path]

        # Move up one level
        parent = current.parent
        if parent == current:  # Reached filesystem root
            break
        current = parent

    # If we couldn't find it, raise an error
    raise RuntimeError(
        "Could not find repository root. "
        "Expected to find a directory containing 'src' and/or 'include' folders."
    )


def main():
    # Change to the script's directory
    script_dir = Path(__file__).parent.resolve()
    os.chdir(script_dir)

    # Clean up and create results directory.
    results_dir = script_dir / "results"
    if results_dir.exists():
        import shutil

        shutil.rmtree(results_dir)
    results_dir.mkdir()

    # Find the repository root by searching for src and include directories.
    try:
        repo_root, scan_dirs = find_repository_directories(script_dir)

        print(f"Found repository root: {repo_root}")
        print(f"Scanning directories:")
        for scan_dir in scan_dirs:
            print(f"  - {scan_dir.relative_to(repo_root)}")
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    print("\nScanning for raw includes...")
    # Find all #include directives
    raw_includes: List[Tuple[str, str]] = []
    rawincludes_file = results_dir / "rawincludes.txt"

    # Write to file as we go to avoid storing everything in memory.
    with open(rawincludes_file, "w", buffering=8192) as raw_f:
        for dir_path in scan_dirs:
            print(f"  Scanning {dir_path.relative_to(repo_root)}...")

            for file_path in dir_path.rglob("*"):
                if not file_path.is_file():
                    continue

                try:
                    rel_path_str = str(file_path.relative_to(repo_root))

                    # Read file with a large buffer for performance.
                    with open(
                        file_path,
                        "r",
                        encoding="utf-8",
                        errors="ignore",
                        buffering=8192,
                    ) as f:
                        for line in f:
                            # Quick check before regex
                            if "#include" not in line or "boost" in line:
                                continue

                            if INCLUDE_PATTERN.match(line):
                                line_stripped = line.strip()
                                entry = f"{rel_path_str}:{line_stripped}\n"
                                print(entry, end="")
                                raw_f.write(entry)
                                raw_includes.append((rel_path_str, line_stripped))
                except Exception as e:
                    print(f"Error reading {file_path}: {e}", file=sys.stderr)

    # Build levelization paths and count directly (no need to sort first).
    print("Build levelization paths")
    path_counts: Dict[Tuple[str, str], int] = defaultdict(int)

    for file_path, include_line in raw_includes:
        include_level = extract_include_level(include_line)
        if not include_level:
            continue

        level = get_level(file_path)
        if level != include_level:
            path_counts[(level, include_level)] += 1

    # Sort and deduplicate paths (using dictionary order like bash 'sort -d').
    print("Sort and deduplicate paths")

    paths_file = results_dir / "paths.txt"
    with open(paths_file, "w") as f:
        # Sort using dictionary order: only alphanumeric and spaces matter
        sorted_items = sorted(
            path_counts.items(),
            key=lambda x: (dictionary_sort_key(x[0][0]), dictionary_sort_key(x[0][1])),
        )
        for (level, include_level), count in sorted_items:
            line = f"{count:7} {level} {include_level}\n"
            print(line.rstrip())
            f.write(line)

    # Split into flat-file database
    print("Split into flat-file database")
    includes_dir = results_dir / "includes"
    included_by_dir = results_dir / "included_by"
    includes_dir.mkdir()
    included_by_dir.mkdir()

    # Batch writes by grouping data first to avoid repeated file opens.
    includes_data: Dict[str, List[Tuple[str, int]]] = defaultdict(list)
    included_by_data: Dict[str, List[Tuple[str, int]]] = defaultdict(list)

    # Process in sorted order to match bash script behaviour (dictionary order).
    sorted_items = sorted(
        path_counts.items(),
        key=lambda x: (dictionary_sort_key(x[0][0]), dictionary_sort_key(x[0][1])),
    )
    for (level, include_level), count in sorted_items:
        includes_data[level].append((include_level, count))
        included_by_data[include_level].append((level, count))

    # Write all includes files in sorted order (dictionary order).
    for level in sorted(includes_data.keys(), key=dictionary_sort_key):
        entries = includes_data[level]
        with open(includes_dir / level, "w") as f:
            for include_level, count in entries:
                line = f"{include_level} {count}\n"
                print(line.rstrip())
                f.write(line)

    # Write all included_by files in sorted order (dictionary order).
    for include_level in sorted(included_by_data.keys(), key=dictionary_sort_key):
        entries = included_by_data[include_level]
        with open(included_by_dir / include_level, "w") as f:
            for level, count in entries:
                line = f"{level} {count}\n"
                print(line.rstrip())
                f.write(line)

    # Search for loops
    print("Search for loops")
    loops_file = results_dir / "loops.txt"
    ordering_file = results_dir / "ordering.txt"

    loops_found: Set[Tuple[str, str]] = set()

    # Pre-load all include files into memory to avoid repeated I/O.
    # This is the biggest optimisation - we were reading files repeatedly in nested loops.
    # Use list of tuples to preserve file order.
    includes_cache: Dict[str, List[Tuple[str, int]]] = {}
    includes_lookup: Dict[str, Dict[str, int]] = {}  # For fast lookup

    # Note: bash script uses 'for source in *' which uses standard glob sorting,
    # NOT dictionary order. So we use standard sorted() here, not dictionary_sort_key.
    for include_file in sorted(includes_dir.iterdir(), key=lambda p: p.name):
        if not include_file.is_file():
            continue

        includes_cache[include_file.name] = []
        includes_lookup[include_file.name] = {}
        with open(include_file, "r") as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 2:
                    include_name = parts[0]
                    include_count = int(parts[1])
                    includes_cache[include_file.name].append(
                        (include_name, include_count)
                    )
                    includes_lookup[include_file.name][include_name] = include_count

    with open(loops_file, "w", buffering=8192) as loops_f, open(
        ordering_file, "w", buffering=8192
    ) as ordering_f:

        # Use standard sorting to match bash glob expansion 'for source in *'.
        for source in sorted(includes_cache.keys()):
            source_includes = includes_cache[source]

            for include, include_freq in source_includes:
                # Check if include file exists and references source
                if include not in includes_lookup:
                    continue

                source_freq = includes_lookup[include].get(source)

                if source_freq is not None:
                    # Found a loop
                    loop_key = tuple(sorted([source, include]))
                    if loop_key in loops_found:
                        continue
                    loops_found.add(loop_key)

                    loops_f.write(f"Loop: {source} {include}\n")

                    # If the counts are close, indicate that the two modules are
                    # on the same level, though they shouldn't be.
                    diff = include_freq - source_freq
                    if diff > 3:
                        loops_f.write(f"  {source} > {include}\n\n")
                    elif diff < -3:
                        loops_f.write(f"  {include} > {source}\n\n")
                    elif source_freq == include_freq:
                        loops_f.write(f"  {include} == {source}\n\n")
                    else:
                        loops_f.write(f"  {include} ~= {source}\n\n")
                else:
                    ordering_f.write(f"{source} > {include}\n")

    # Print results
    print("\nOrdering:")
    with open(ordering_file, "r") as f:
        print(f.read(), end="")

    print("\nLoops:")
    with open(loops_file, "r") as f:
        print(f.read(), end="")


if __name__ == "__main__":
    main()
