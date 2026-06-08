#!/usr/bin/env python3

"""
Checks that a pull request description has been customized from the
pull_request_template.md. Exits with code 1 if the description is empty
or identical to the template (ignoring HTML comments and whitespace).

Usage:
    python check-pr-description.py --template-file TEMPLATE --pr-body-file BODY
"""

import argparse
import re
import sys
from pathlib import Path


def normalize(text: str) -> str:
    """Strip HTML comments, trim lines, and remove blank lines."""
    # Remove HTML comments (possibly multi-line)
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)
    # Strip each line and drop empties
    lines = [line.strip() for line in text.splitlines()]
    lines = [line for line in lines if line]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that a PR description differs from the template."
    )
    parser.add_argument(
        "--template-file",
        type=Path,
        required=True,
        help="Path to the pull request template file.",
    )
    parser.add_argument(
        "--pr-body-file",
        type=Path,
        required=True,
        help="Path to a file containing the PR body text.",
    )
    args = parser.parse_args()

    template_path: Path = args.template_file
    pr_body_path: Path = args.pr_body_file

    if not template_path.is_file():
        print(f"::error::Template file {template_path} not found")
        return 1

    if not pr_body_path.is_file():
        print(f"::error::PR body file {pr_body_path} not found")
        return 1

    template = template_path.read_text(encoding="utf-8")
    pr_body = pr_body_path.read_text(encoding="utf-8")

    # Check if the PR body is empty or whitespace-only
    if not pr_body.strip():
        print(
            "::error::PR description is empty. "
            "Please fill in the pull request template."
        )
        return 1

    norm_template = normalize(template)
    norm_pr_body = normalize(pr_body)

    if norm_pr_body == norm_template:
        print(
            "::error::PR description (ignoring HTML comments) is identical"
            " to the template. Please fill in the details of your change."
            f"\n\nVisible template content:\n---\n{norm_template}\n---"
            f"\n\nVisible PR description content:\n---\n{norm_pr_body}\n---"
        )
        return 1

    print("PR description has been customized from the template.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
