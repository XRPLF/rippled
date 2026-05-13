#!/usr/bin/env node
/**
 * xrpld doc-agent CLI entry point.
 *
 * @example
 *   doc-agent document src/libxrpl/basics/base_uint.h
 *   doc-agent document include/xrpl/basics/
 *   doc-agent review develop..HEAD
 *   doc-agent review --pr 1234
 */

import { documentTarget } from './document.js';
import { reviewDiff } from './review.js';

const USAGE = `
xrpld doc-agent

Usage:
  doc-agent document <file-or-directory>   Add Doxygen documentation
  doc-agent review <base>..<head>          Detect doc drift in range
  doc-agent review --pr <number>           Detect doc drift for a PR

Environment:
  ANTHROPIC_API_KEY  (required)  Anthropic API key
  XRPLD_ROOT         (optional)  Path to xrpld repo root (default: repo root)
  DOC_AGENT_MODEL    (optional)  Model override (default: claude-opus-4-7)
`;

function printUsageAndExit(code: number): never {
  console.error(USAGE);
  process.exit(code);
}

const HELP_MODES: ReadonlySet<string> = new Set(['help', '--help', '-h']);

async function main(): Promise<void> {
  const [mode, ...args] = process.argv.slice(2);

  if (process.env['ANTHROPIC_API_KEY'] === undefined) {
    console.error('ERROR: ANTHROPIC_API_KEY environment variable is required.');
    process.exit(1);
  }

  if (mode === undefined || HELP_MODES.has(mode)) {
    printUsageAndExit(0);
  }

  if (mode === 'document') {
    const target = args[0];
    if (target === undefined) printUsageAndExit(1);
    await documentTarget(target);
    return;
  }

  if (mode === 'review') {
    if (args.length === 0) printUsageAndExit(1);
    await reviewDiff(args);
    return;
  }

  console.error(`Unknown mode: ${mode}`);
  printUsageAndExit(1);
}

main().catch((err: unknown) => {
  const message = err instanceof Error ? (err.stack ?? err.message) : String(err);
  console.error('FATAL:', message);
  process.exit(1);
});
