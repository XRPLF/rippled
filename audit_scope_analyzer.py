#!/usr/bin/env python3
"""
XRPL Lending Protocol - Audit Scope Analyzer
Extracts all functions from the lending protocol implementation for systematic security review.
"""

import re
import os
from pathlib import Path
from collections import defaultdict
import json

class LendingAuditAnalyzer:
    def __init__(self, repo_path):
        self.repo_path = Path(repo_path)
        self.functions = defaultdict(list)

    def find_lending_files(self):
        """Find all files related to lending protocol."""
        patterns = [
            "src/xrpld/app/tx/detail/Loan*.cpp",
            "src/xrpld/app/tx/detail/Loan*.h",
            "src/xrpld/app/misc/Lending*.cpp",
            "src/xrpld/app/misc/Lending*.h",
            "src/test/app/Loan*.cpp",
            "include/xrpl/protocol/*Loan*.h",
        ]

        files = []
        for pattern in patterns:
            files.extend(self.repo_path.glob(pattern))

        return sorted(set(files))

    def extract_functions(self, file_path):
        """Extract function declarations and definitions from a C++ file."""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            print(f"Error reading {file_path}: {e}")
            return []

        functions = []

        # Pattern for function declarations/definitions
        # Matches: ReturnType functionName(params) const/noexcept/etc
        pattern = re.compile(
            r'^\s*(?:(?:virtual|static|inline|explicit|constexpr|friend)\s+)*'  # modifiers
            r'([A-Za-z_][A-Za-z0-9_:<>]*(?:\s*[*&]+)?)\s+'  # return type
            r'([A-Za-z_][A-Za-z0-9_]*)\s*'  # function name
            r'\(([^)]*)\)'  # parameters
            r'([^;{]*?)'  # trailing specifiers (const, noexcept, override, etc)
            r'(?:\s*[;{])',  # ends with ; or {
            re.MULTILINE
        )

        lines = content.split('\n')
        for i, line in enumerate(lines, 1):
            # Skip comments
            if line.strip().startswith('//') or line.strip().startswith('/*') or line.strip().startswith('*'):
                continue

            match = pattern.search(line)
            if match:
                return_type = match.group(1).strip()
                func_name = match.group(2).strip()
                params = match.group(3).strip()
                specifiers = match.group(4).strip()

                # Skip constructors, destructors, operators
                if func_name.startswith('~') or func_name.startswith('operator'):
                    continue

                # Check if it's a significant function (not just getters/setters)
                is_critical = any([
                    'check' in func_name.lower(),
                    'validate' in func_name.lower(),
                    'apply' in func_name.lower(),
                    'pre' in func_name.lower(),
                    'do' in func_name.lower(),
                    'calculate' in func_name.lower(),
                    'delete' in func_name.lower(),
                    'set' in func_name.lower() and len(func_name) > 3,
                    'manage' in func_name.lower(),
                    'pay' in func_name.lower(),
                    'cover' in func_name.lower(),
                    'broker' in func_name.lower(),
                    'loan' in func_name.lower(),
                    'impair' in func_name.lower(),
                    'rate' in func_name.lower(),
                ])

                functions.append({
                    'name': func_name,
                    'return_type': return_type,
                    'params': params,
                    'specifiers': specifiers,
                    'line': i,
                    'is_critical': is_critical,
                    'signature': f"{return_type} {func_name}({params}) {specifiers}".strip()
                })

        return functions

    def categorize_function(self, func_name, file_path):
        """Categorize function by its role in the lending protocol."""
        fname = func_name.lower()
        fpath = str(file_path).lower()

        if 'test' in fpath:
            return 'test'
        elif any(x in fname for x in ['preflight', 'preclaim']):
            return 'validation'
        elif 'doapply' in fname or 'apply' in fname:
            return 'core_execution'
        elif any(x in fname for x in ['check', 'validate', 'verify']):
            return 'validation'
        elif any(x in fname for x in ['calculate', 'compute']):
            return 'arithmetic'
        elif 'helper' in fpath or any(x in fname for x in ['helper', 'util']):
            return 'helpers'
        elif any(x in fname for x in ['loanset', 'loandelete', 'loanmanage', 'loanpay']):
            return 'transaction_handlers'
        elif 'broker' in fname:
            return 'broker_operations'
        else:
            return 'other'

    def analyze(self):
        """Analyze all lending protocol files."""
        files = self.find_lending_files()

        print(f"Found {len(files)} lending protocol files\n")

        for file_path in files:
            print(f"Analyzing: {file_path.name}")
            functions = self.extract_functions(file_path)

            for func in functions:
                category = self.categorize_function(func['name'], file_path)
                self.functions[category].append({
                    **func,
                    'file': str(file_path.relative_to(self.repo_path)),
                    'file_name': file_path.name
                })

        return self.functions

    def generate_markdown_report(self, output_path):
        """Generate a markdown report of all functions in audit scope."""
        with open(output_path, 'w') as f:
            f.write("# XRPL Lending Protocol - Audit Scope Function Inventory\n\n")
            f.write("Generated by audit_scope_analyzer.py\n\n")
            f.write("---\n\n")

            # Summary statistics
            total = sum(len(funcs) for funcs in self.functions.values())
            critical = sum(1 for funcs in self.functions.values() for func in funcs if func['is_critical'])

            f.write(f"## Summary\n\n")
            f.write(f"- **Total Functions**: {total}\n")
            f.write(f"- **Critical Functions**: {critical}\n")
            f.write(f"- **Categories**: {len(self.functions)}\n\n")

            # Table of contents
            f.write("## Table of Contents\n\n")
            categories = sorted(self.functions.keys())
            for i, category in enumerate(categories, 1):
                count = len(self.functions[category])
                f.write(f"{i}. [{category.replace('_', ' ').title()}](#{category}) ({count} functions)\n")
            f.write("\n---\n\n")

            # Detailed listings
            for category in categories:
                funcs = sorted(self.functions[category], key=lambda x: (x['file_name'], x['line']))

                f.write(f"## {category.replace('_', ' ').title()}\n\n")
                f.write(f"**Count**: {len(funcs)} functions\n\n")

                # Group by file
                by_file = defaultdict(list)
                for func in funcs:
                    by_file[func['file']].append(func)

                for file_path in sorted(by_file.keys()):
                    file_funcs = by_file[file_path]
                    f.write(f"### `{file_path}`\n\n")

                    for func in file_funcs:
                        critical_marker = " 🔴" if func['is_critical'] else ""
                        f.write(f"#### `{func['name']}`{critical_marker}\n\n")
                        f.write(f"- **Location**: Line {func['line']}\n")
                        f.write(f"- **Signature**: `{func['signature']}`\n")
                        f.write(f"- **Return Type**: `{func['return_type']}`\n")

                        if func['params']:
                            f.write(f"- **Parameters**: `{func['params']}`\n")

                        f.write("\n**Audit Notes**:\n")
                        f.write("- [ ] Reviewed\n")
                        f.write("- [ ] Validated input handling\n")
                        f.write("- [ ] Checked arithmetic safety\n")
                        f.write("- [ ] Verified state consistency\n")
                        f.write("\n---\n\n")

            f.write("\n## Legend\n\n")
            f.write("- 🔴 = Critical function requiring detailed security review\n")
            f.write("- [ ] = Audit checklist item\n")

    def generate_json_report(self, output_path):
        """Generate JSON report for programmatic processing."""
        data = {
            'total_functions': sum(len(funcs) for funcs in self.functions.values()),
            'categories': {cat: len(funcs) for cat, funcs in self.functions.items()},
            'functions': dict(self.functions)
        }

        with open(output_path, 'w') as f:
            json.dump(data, f, indent=2)

def main():
    repo_path = Path(__file__).parent
    analyzer = LendingAuditAnalyzer(repo_path)

    print("=" * 70)
    print("XRPL Lending Protocol - Audit Scope Analyzer")
    print("=" * 70)
    print()

    functions = analyzer.analyze()

    print("\n" + "=" * 70)
    print("Analysis Complete!")
    print("=" * 70)
    print()

    # Generate reports
    md_output = repo_path / "AUDIT_SCOPE.md"
    json_output = repo_path / "audit_scope.json"

    analyzer.generate_markdown_report(md_output)
    analyzer.generate_json_report(json_output)

    print(f"✓ Markdown report: {md_output}")
    print(f"✓ JSON report: {json_output}")
    print()

    # Print summary
    print("Category Summary:")
    for category, funcs in sorted(functions.items()):
        critical = sum(1 for f in funcs if f['is_critical'])
        print(f"  {category:30s}: {len(funcs):3d} functions ({critical} critical)")

if __name__ == '__main__':
    main()
