/**
 * Review mode: detect documentation drift in a git diff range.
 *
 * Used by the doc-review GitHub Action and locally for testing.
 */

import { execSync } from 'node:child_process';
import { writeFile } from 'node:fs/promises';
import { query } from '@anthropic-ai/claude-agent-sdk';
import { MODEL, XRPLD_ROOT } from './config.js';
import { loadSystemPrompt } from './prompt-loader.js';
import type { FileReviewResult, GitRange, ReviewIssue, ReviewOutput } from './types.js';

const MAX_DIFF_CHARS = 12_000;
const TRACKED_PATH_PATTERN = /^(include|src\/libxrpl|src\/xrpld)\//;
const CPP_FILE_PATTERN = /\.(h|hpp|cpp)$/;

/**
 * Parse the CLI arguments into a base..head git range.
 *
 * Accepts either:
 *   - `base..head` (e.g. `develop..HEAD`)
 *   - `--pr <number>` (resolves via `gh pr view`)
 */
function parseRangeArgs(args: readonly string[]): GitRange {
  const first = args[0];
  if (first === undefined) {
    throw new Error('Expected range as base..head or --pr <number>');
  }

  if (first === '--pr') {
    const pr = args[1];
    if (pr === undefined) {
      throw new Error('--pr requires a PR number');
    }
    const base = execSync(`gh pr view ${pr} --json baseRefOid -q .baseRefOid`, {
      cwd: XRPLD_ROOT,
    })
      .toString()
      .trim();
    const head = execSync(`gh pr view ${pr} --json headRefOid -q .headRefOid`, {
      cwd: XRPLD_ROOT,
    })
      .toString()
      .trim();
    return { base, head };
  }

  const match = first.match(/^([^.]+)\.\.([^.]+)$/);
  if (match === null || match[1] === undefined || match[2] === undefined) {
    throw new Error('Expected range as base..head or --pr <number>');
  }
  return { base: match[1], head: match[2] };
}

/**
 * Get the list of C++ source files changed in the given git range,
 * filtered to paths the doc-agent cares about.
 */
function getChangedCppFiles(range: GitRange): string[] {
  const out = execSync(`git diff --name-only ${range.base}...${range.head}`, {
    cwd: XRPLD_ROOT,
  }).toString();

  return out
    .split('\n')
    .filter((line) => line.length > 0)
    .filter((file) => CPP_FILE_PATTERN.test(file))
    .filter((file) => TRACKED_PATH_PATTERN.test(file));
}

/** Get the unified diff for a single file in the given range. */
function getFileDiff(range: GitRange, file: string): string {
  return execSync(`git diff ${range.base}...${range.head} -- "${file}"`, {
    cwd: XRPLD_ROOT,
    maxBuffer: 10 * 1024 * 1024,
  }).toString();
}

/** Extract a JSON object from a possibly-fenced model response. */
function extractJson(response: string): ReviewOutput | null {
  const fenced = response.match(/```json\s*([\s\S]*?)```/);
  const raw = fenced?.[1] ?? response.match(/(\{[\s\S]*\})/)?.[1];
  if (raw === undefined) return null;
  try {
    return JSON.parse(raw) as ReviewOutput;
  } catch {
    return null;
  }
}

/** Send one file's diff to the agent and parse the response. */
async function reviewFile(range: GitRange, file: string): Promise<FileReviewResult | null> {
  console.log(`\n=== Reviewing: ${file} ===`);
  const diff = getFileDiff(range, file);
  if (diff.trim().length === 0) return null;

  const systemPrompt = await loadSystemPrompt('review-diff', file);
  const userPrompt = `Review this diff for documentation drift:

## File: ${file}

## Diff
\`\`\`
${diff.slice(0, MAX_DIFF_CHARS)}
\`\`\`

Use the Read tool to inspect the current state of the file, related tests,
or callers if needed. Output findings as JSON per the schema in the system
prompt.`;

  let response = '';
  const result = query({
    prompt: userPrompt,
    options: {
      model: MODEL,
      systemPrompt,
      cwd: XRPLD_ROOT,
      allowedTools: ['Read', 'Glob', 'Grep', 'Bash'],
      permissionMode: 'acceptEdits',
    },
  });

  for await (const message of result) {
    if (message.type === 'assistant') {
      const content = message.message?.content;
      if (Array.isArray(content)) {
        for (const block of content) {
          if (block.type === 'text') {
            response += block.text;
          }
        }
      }
    }
  }

  const parsed = extractJson(response);
  if (parsed === null) {
    console.warn(`  No JSON output for ${file}, skipping`);
    return null;
  }

  const issues: ReviewIssue[] = parsed.issues.map((issue) => ({
    file: issue.file ?? file,
    line: issue.line,
    severity: issue.severity,
    message: issue.message,
    ...(issue.suggested_doc !== undefined && { suggestedDoc: issue.suggested_doc }),
  }));

  return { file, summary: parsed.summary, issues };
}

/** Build the markdown report posted to the PR. */
function buildReport(fileCount: number, results: readonly FileReviewResult[]): string {
  const issues = results.flatMap((r) => r.issues);
  const warnings = issues.filter((i) => i.severity === 'warning').length;
  const suggestions = issues.length - warnings;

  const lines: string[] = ['## Documentation Review Report', ''];
  lines.push(
    issues.length === 0
      ? 'No documentation issues found.'
      : `Found **${issues.length}** issue(s) across **${fileCount}** changed file(s): ${warnings} warning(s), ${suggestions} suggestion(s).`,
  );
  lines.push('');

  for (const result of results) {
    if (result.issues.length === 0) continue;
    lines.push(`### \`${result.file}\``, '', result.summary, '');
    for (const issue of result.issues) {
      const tag = issue.severity === 'warning' ? '**Warning:**' : '**Suggestion:**';
      lines.push(`- ${tag} Line ${issue.line}: ${issue.message}`);
    }
    lines.push('');
  }

  lines.push('---', '*Automated review by doc-agent.*');
  return lines.join('\n');
}

/**
 * Review the documentation drift introduced by a git range or PR.
 *
 * Writes two output files in the current working directory:
 *   - doc-review-report.md (markdown summary for PR comment)
 *   - doc-review-comments.json (inline review comments)
 */
export async function reviewDiff(args: readonly string[]): Promise<void> {
  const range = parseRangeArgs(args);
  console.log(`Reviewing range: ${range.base}...${range.head}`);

  const files = getChangedCppFiles(range);
  if (files.length === 0) {
    console.log('No C++ files changed in this range.');
    await writeFile('doc-review-report.md', '## Documentation Review\n\nNo C++ files changed.\n');
    await writeFile('doc-review-comments.json', '[]');
    return;
  }

  console.log(`Found ${files.length} changed C++ file(s).`);

  const results: FileReviewResult[] = [];
  for (const file of files) {
    try {
      const result = await reviewFile(range, file);
      if (result !== null) results.push(result);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      console.warn(`  Review failed for ${file}: ${message}`);
    }
  }

  const report = buildReport(files.length, results);
  const allIssues = results.flatMap((r) => r.issues);

  // Shape inline comments for the GitHub pulls.createReviewComment API,
  // which expects `path`/`line`/`body` rather than the internal field names.
  const apiComments = allIssues.map((issue) => ({
    path: issue.file,
    line: issue.line,
    body: issue.suggestedDoc
      ? `**[${issue.severity}]** ${issue.message}\n\n\`\`\`cpp\n${issue.suggestedDoc}\n\`\`\``
      : `**[${issue.severity}]** ${issue.message}`,
  }));

  await writeFile('doc-review-report.md', report);
  await writeFile('doc-review-comments.json', JSON.stringify(apiComments, null, 2));

  console.log('\nReport: doc-review-report.md');
  console.log(`Inline comments: doc-review-comments.json (${apiComments.length} issues)`);
}
