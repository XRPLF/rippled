#!/bin/bash -e
# Generates CHANGELOG.md for SOCI project.
#
# Runs github-changelog-generator to automatically generate changelog
# from your tags, closed issues and merged pull requests.
#
# Requirements:
# - https://github.com/skywinder/github-changelog-generator
#
# Copyright (c) 2017 Mateusz Loskot <mateusz@loskot.net>
#
if [[ -z "$CHANGELOG_GITHUB_TOKEN" ]]; then
    echo "Environment variable CHANGELOG_GITHUB_TOKEN not found!" >&2
    echo "GitHub API token is required to avoid hitting limit of requests per hour." >&2
    exit 1
fi

if ! type "github_changelog_generator" > /dev/null; then
    echo "github_changelog_generator not found." >&2
    echo "Go to https://github.com/skywinder/github-changelog-generator"
    exit 1
fi

github_changelog_generator --verbose --date-format "%Y-%m-%d"
