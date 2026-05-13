#!/usr/bin/env python3
"""
Diff-aware documentation review for xrpld PRs.

For each changed C++ file, extracts the diff hunks and existing doc
comments, then asks the Anthropic API whether documentation needs
updating. Produces:
  - doc-review-report.md: summary comment for the PR
  - doc-review-comments.json: inline review comments with file/line info
"""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import anthropic
except ImportError:
    print("ERROR: anthropic package not installed. Run: pip install anthropic")
    sys.exit(1)

MODEL = "claude-sonnet-4-6"
MAX_TOKENS = 2048

SYSTEM_PROMPT = """You are a documentation reviewer for the xrpld (XRP Ledger daemon) C++ codebase.

Your job is to review code changes and determine whether existing documentation
comments need updating, or whether new documentation is needed.

Documentation style: Javadoc-style Doxygen comments (/** ... */).
See the project's docs/DOCUMENTATION_STANDARDS.md for full guidelines.

Rules:
- Only flag REAL semantic drift: changed behavior, new parameters, removed
  functionality, changed return values, new error conditions.
- Do NOT flag cosmetic changes (whitespace, formatting, variable renames that
  don't change semantics).
- Do NOT suggest docs for private implementation details unless the logic is
  genuinely non-obvious.
- Do NOT paraphrase function signatures. Good docs explain WHY and WHAT
  BEHAVIOR, not WHAT THE CODE LITERALLY DOES.
- Be terse. Each finding should be 1-3 sentences.

For each issue found, respond with a JSON array of objects:
{
  "issues": [
    {
      "file": "path/to/file.h",
      "line": 42,
      "severity": "warning" | "suggestion",
      "message": "Brief description of the doc issue",
      "suggested_doc": "Optional: suggested doc comment text"
    }
  ],
  "summary": "One-paragraph summary of documentation state for this file"
}

If no issues are found, return: {"issues": [], "summary": "Documentation is up to date."}
Respond ONLY with valid JSON. No markdown fences, no explanation outside JSON."""


@dataclass
class FileAnalysis:
    path: str
    diff: str
    existing_docs: str
    file_content: str


def get_diff(base_sha: str, head_sha: str, filepath: str) -> str:
    """Get the unified diff for a specific file between two commits."""
    try:
        result = subprocess.run(
            ["git", "diff", f"{base_sha}...{head_sha}", "--", filepath],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout
    except subprocess.CalledProcessError:
        return ""


def extract_doc_comments(content: str) -> str:
    """Extract all /** ... */ doc comments from file content."""
    pattern = r'/\*\*[\s\S]*?\*/'
    matches = re.findall(pattern, content)
    return "\n\n".join(matches) if matches else "(no documentation comments found)"


def read_file_safe(filepath: str) -> str:
    """Read a file, returning empty string if it doesn't exist."""
    try:
        return Path(filepath).read_text(encoding="utf-8", errors="replace")
    except (FileNotFoundError, PermissionError):
        return ""


def analyze_file(client: anthropic.Anthropic, analysis: FileAnalysis) -> dict:
    """Send a file's diff and docs to the API for review."""
    user_prompt = f"""Review the following code change for documentation accuracy.

## File: {analysis.path}

## Git Diff:
```
{analysis.diff[:8000]}
```

## Existing Documentation Comments:
```
{analysis.existing_docs[:4000]}
```

## Current File Content (first 200 lines for context):
```cpp
{chr(10).join(analysis.file_content.split(chr(10))[:200])}
```

Analyze whether the diff introduces changes that make existing docs inaccurate,
or adds new public API surface that lacks documentation."""

    try:
        response = client.messages.create(
            model=MODEL,
            max_tokens=MAX_TOKENS,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": user_prompt}],
        )
        text = response.content[0].text.strip()
        if text.startswith("```"):
            text = re.sub(r'^```\w*\n?', '', text)
            text = re.sub(r'\n?```$', '', text)
        return json.loads(text)
    except (json.JSONDecodeError, Exception) as e:
        return {
            "issues": [],
            "summary": f"Analysis failed: {str(e)[:200]}",
        }


def generate_report(
    results: dict[str, dict],
    changed_files: list[str],
) -> str:
    """Generate the markdown summary report."""
    lines = ["## Documentation Review Report", ""]

    total_issues = sum(len(r.get("issues", [])) for r in results.values())
    warnings = sum(
        1
        for r in results.values()
        for i in r.get("issues", [])
        if i.get("severity") == "warning"
    )
    suggestions = total_issues - warnings

    if total_issues == 0:
        lines.append("No documentation issues found.")
    else:
        lines.append(
            f"Found **{total_issues}** documentation issue(s) "
            f"across **{len(changed_files)}** changed file(s): "
            f"{warnings} warning(s), {suggestions} suggestion(s)."
        )

    lines.append("")
    lines.append(f"Files reviewed: {len(changed_files)}")
    lines.append("")

    for filepath, result in sorted(results.items()):
        issues = result.get("issues", [])
        summary = result.get("summary", "")
        if issues:
            lines.append(f"### `{filepath}`")
            lines.append("")
            lines.append(summary)
            lines.append("")
            for issue in issues:
                severity = issue.get("severity", "suggestion")
                icon = "**Warning:**" if severity == "warning" else "**Suggestion:**"
                line_num = issue.get("line", "?")
                msg = issue.get("message", "")
                lines.append(f"- {icon} Line {line_num}: {msg}")
            lines.append("")

    lines.append("---")
    lines.append(
        "*Automated documentation review. "
        "See [docs/DOCUMENTATION_STANDARDS.md](../docs/DOCUMENTATION_STANDARDS.md) "
        "for guidelines.*"
    )

    return "\n".join(lines)


def generate_inline_comments(results: dict[str, dict]) -> list[dict]:
    """Generate inline PR review comments from analysis results."""
    comments = []
    for filepath, result in results.items():
        for issue in result.get("issues", []):
            line = issue.get("line")
            if not line or not isinstance(line, int):
                continue

            body = issue.get("message", "")
            suggested = issue.get("suggested_doc")
            if suggested:
                body += f"\n\n**Suggested documentation:**\n```cpp\n{suggested}\n```"

            severity = issue.get("severity", "suggestion")
            prefix = "Doc Warning" if severity == "warning" else "Doc Suggestion"
            body = f"**{prefix}:** {body}"

            comments.append({"path": filepath, "line": line, "body": body})

    return comments


def main():
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("ERROR: ANTHROPIC_API_KEY not set")
        sys.exit(1)

    changed_files_str = os.environ.get("CHANGED_FILES", "")
    if not changed_files_str:
        print("No changed files to review")
        sys.exit(0)

    base_sha = os.environ.get("BASE_SHA", "HEAD~1")
    head_sha = os.environ.get("HEAD_SHA", "HEAD")

    changed_files = [f.strip() for f in changed_files_str.split() if f.strip()]
    cpp_files = [
        f for f in changed_files if f.endswith((".h", ".hpp", ".cpp"))
    ]

    if not cpp_files:
        print("No C++ files changed")
        sys.exit(0)

    print(f"Reviewing {len(cpp_files)} file(s) for documentation accuracy...")

    client = anthropic.Anthropic(api_key=api_key)
    results = {}

    for filepath in cpp_files:
        print(f"  Analyzing: {filepath}")
        diff = get_diff(base_sha, head_sha, filepath)
        if not diff:
            continue

        content = read_file_safe(filepath)
        existing_docs = extract_doc_comments(content)

        analysis = FileAnalysis(
            path=filepath,
            diff=diff,
            existing_docs=existing_docs,
            file_content=content,
        )
        results[filepath] = analyze_file(client, analysis)

    report = generate_report(results, cpp_files)
    Path("doc-review-report.md").write_text(report)
    print("\nReport written to doc-review-report.md")

    comments = generate_inline_comments(results)
    Path("doc-review-comments.json").write_text(json.dumps(comments, indent=2))
    print(f"Generated {len(comments)} inline comment(s)")


if __name__ == "__main__":
    main()
