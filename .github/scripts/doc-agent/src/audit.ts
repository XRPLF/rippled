/**
 * Audit mode: measure how completely each file's Doxygen documentation
 * reflects the authoritative design intent in its sibling .ai.md.
 *
 * For each C++ file under the target that has a .ai.md sibling:
 *   - Locate its header/source partner (if any) and the partner's .ai.md.
 *   - Send primary + partner files and both .ai.md files to the agent.
 *   - Parse a structured JSON verdict per file.
 *
 * Writes:
 *   - doc-audit-report.json  Aggregated per-file results.
 *   - doc-audit-report.md    Human-readable summary.
 */

import { existsSync, readdirSync, statSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';
import { join, relative, resolve } from 'node:path';
import { query } from '@anthropic-ai/claude-agent-sdk';
import { MODEL, XRPLD_ROOT } from './config.js';
import { findPartner } from './pairing.js';
import { loadSystemPrompt } from './prompt-loader.js';

const SOURCE_EXTS: ReadonlySet<string> = new Set(['.h', '.hpp', '.cpp']);
const MAX_FILE_CHARS = 24_000;
const MAX_AI_MD_CHARS = 16_000;
const DEFAULT_CONCURRENCY = 5;

interface AuditMissed {
  function: string;
  topic: string;
  home: 'header' | 'source' | 'either';
  current_state: 'absent' | 'wrong-home' | 'thin';
  ai_md_quote: string;
}

interface AuditResult {
  file: string;
  ai_md_concepts: number;
  translated: number;
  missed: AuditMissed[];
  verdict: 'rerun' | 'leave';
}

/**
 * Recursively find C++ source files under a target path that have a
 * sibling .ai.md.
 */
function findAuditTargets(target: string): string[] {
  const absTarget = resolve(XRPLD_ROOT, target);
  if (!existsSync(absTarget)) {
    throw new Error(`Target does not exist: ${absTarget}`);
  }

  const out: string[] = [];
  const consider = (file: string): void => {
    const dotIdx = file.lastIndexOf('.');
    if (dotIdx === -1) return;
    const ext = file.slice(dotIdx);
    if (!SOURCE_EXTS.has(ext)) return;
    if (!existsSync(`${file}.ai.md`)) return;
    out.push(file);
  };

  const stat = statSync(absTarget);
  if (stat.isFile()) {
    consider(absTarget);
    return out;
  }

  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.isFile()) consider(full);
    }
  };
  walk(absTarget);
  return out;
}

/** Read a file, capping at maxChars to keep prompts within budget. */
async function readCapped(absPath: string, maxChars: number): Promise<string> {
  const text = await readFile(absPath, 'utf8');
  if (text.length <= maxChars) return text;
  return `${text.slice(0, maxChars)}\n\n... [truncated, ${text.length - maxChars} bytes elided] ...`;
}

/** Extract a JSON object from a possibly-fenced model response. */
function extractJson(response: string): AuditResult | null {
  const fenced = response.match(/```json\s*([\s\S]*?)```/);
  const raw = fenced?.[1] ?? response.match(/(\{[\s\S]*\})/)?.[1];
  if (raw === undefined) return null;
  try {
    return JSON.parse(raw) as AuditResult;
  } catch {
    return null;
  }
}

/** Audit a single primary file against its .ai.md and partner context. */
async function auditFile(absPrimary: string): Promise<AuditResult | null> {
  const relPrimary = relative(XRPLD_ROOT, absPrimary);
  console.log(`\n=== Auditing: ${relPrimary} ===`);

  const primary = await readCapped(absPrimary, MAX_FILE_CHARS);
  const primaryAiMd = await readCapped(`${absPrimary}.ai.md`, MAX_AI_MD_CHARS);

  const absPartner = findPartner(absPrimary);
  const relPartner = absPartner === null ? null : relative(XRPLD_ROOT, absPartner);
  const partner = absPartner === null ? null : await readCapped(absPartner, MAX_FILE_CHARS);
  const partnerAiMdPath = absPartner === null ? null : `${absPartner}.ai.md`;
  const partnerAiMd =
    partnerAiMdPath !== null && existsSync(partnerAiMdPath)
      ? await readCapped(partnerAiMdPath, MAX_AI_MD_CHARS)
      : null;

  const partnerBlock =
    relPartner === null || partner === null
      ? ''
      : `

## Partner File (${relPartner})
\`\`\`
${partner}
\`\`\`${
          partnerAiMd === null
            ? ''
            : `

## Partner's .ai.md (${relPartner}.ai.md)
${partnerAiMd}`
        }`;

  const userPrompt = `Audit the documentation coverage of this file against its authoritative .ai.md.

## Primary File (${relPrimary})
\`\`\`
${primary}
\`\`\`

## Primary's .ai.md (${relPrimary}.ai.md)
${primaryAiMd}${partnerBlock}

Output JSON per the schema in the system prompt. The "file" field MUST be
"${relPrimary}".`;

  const systemPrompt = await loadSystemPrompt('audit-file', relPrimary);

  let response = '';
  const result = query({
    prompt: userPrompt,
    options: {
      model: MODEL,
      systemPrompt,
      cwd: XRPLD_ROOT,
      allowedTools: ['Read', 'Glob', 'Grep'],
      permissionMode: 'acceptEdits',
    },
  });

  for await (const message of result) {
    if (message.type === 'assistant') {
      const content = message.message?.content;
      if (Array.isArray(content)) {
        for (const block of content) {
          if (block.type === 'text') response += block.text;
        }
      }
    }
    if (message.type === 'result') {
      const cost = message.total_cost_usd?.toFixed(4) ?? '?';
      const inTok = message.usage?.['input_tokens'] ?? 0;
      const outTok = message.usage?.['output_tokens'] ?? 0;
      console.log(`  [Cost: $${cost}, Tokens: ${inTok}/${outTok}]`);
    }
  }

  const parsed = extractJson(response);
  if (parsed === null) {
    console.warn(`  No JSON output for ${relPrimary}, skipping`);
    return null;
  }
  parsed.file = relPrimary;
  return parsed;
}

/** Render the aggregated markdown report. */
function buildReport(results: readonly AuditResult[]): string {
  const total = results.length;
  const reruns = results.filter((r) => r.verdict === 'rerun');
  const totalConcepts = results.reduce((s, r) => s + r.ai_md_concepts, 0);
  const totalTranslated = results.reduce((s, r) => s + r.translated, 0);
  const overallRate = totalConcepts === 0 ? 0 : Math.round((totalTranslated / totalConcepts) * 100);

  const lines: string[] = [
    '# Documentation Audit Report',
    '',
    `**Files audited:** ${total}`,
    `**Overall translation rate:** ${overallRate}% (${totalTranslated} of ${totalConcepts} .ai.md concepts reflected in docstrings)`,
    `**Files flagged for re-run:** ${reruns.length}`,
    '',
    '## Files flagged for re-run',
    '',
  ];

  if (reruns.length === 0) {
    lines.push('_None — all audited files passed._', '');
  } else {
    lines.push('| File | Translated | Missed | Rate |', '|------|-----------:|-------:|-----:|');
    for (const r of reruns.sort(
      (a, b) =>
        a.translated / Math.max(a.ai_md_concepts, 1) - b.translated / Math.max(b.ai_md_concepts, 1),
    )) {
      const rate = r.ai_md_concepts === 0 ? 0 : Math.round((r.translated / r.ai_md_concepts) * 100);
      lines.push(`| \`${r.file}\` | ${r.translated} | ${r.missed.length} | ${rate}% |`);
    }
    lines.push('', '## Top missed concepts (sampled)', '');
    for (const r of reruns.slice(0, 10)) {
      if (r.missed.length === 0) continue;
      lines.push(`### \`${r.file}\``, '');
      for (const m of r.missed.slice(0, 5)) {
        lines.push(`- **${m.function}** — ${m.topic}`);
        lines.push(`  > ${m.ai_md_quote.replace(/\n/g, ' ').slice(0, 200)}`);
      }
      lines.push('');
    }
  }

  return lines.join('\n');
}

/**
 * Run async work over a list of items with bounded concurrency. Mirrors the
 * minimal slice of p-limit we actually need; collects results in input order.
 */
async function mapWithConcurrency<T, R>(
  items: readonly T[],
  limit: number,
  worker: (item: T, index: number) => Promise<R>,
): Promise<R[]> {
  const results = new Array<R>(items.length);
  let next = 0;

  async function pump(): Promise<void> {
    while (true) {
      const index = next++;
      if (index >= items.length) return;
      // biome-ignore lint/style/noNonNullAssertion: index < items.length
      results[index] = await worker(items[index]!, index);
    }
  }

  const workers = Array.from({ length: Math.min(limit, items.length) }, pump);
  await Promise.all(workers);
  return results;
}

/**
 * Audit every C++ file with a .ai.md sibling under the target path.
 *
 * Concurrency is read from the AUDIT_CONCURRENCY env var (default 5).
 */
export async function auditTarget(target: string): Promise<void> {
  const files = findAuditTargets(target);
  const concurrency = Number(process.env['AUDIT_CONCURRENCY']) || DEFAULT_CONCURRENCY;
  console.log(
    `Found ${files.length} file(s) with .ai.md siblings to audit (concurrency=${concurrency}).`,
  );

  let completed = 0;
  const raw = await mapWithConcurrency(files, concurrency, async (file) => {
    try {
      const result = await auditFile(file);
      completed++;
      console.log(`  Progress: ${completed}/${files.length}`);
      return result;
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      console.warn(`  Audit failed for ${file}: ${message}`);
      completed++;
      console.log(`  Progress: ${completed}/${files.length}`);
      return null;
    }
  });
  const results = raw.filter((r): r is AuditResult => r !== null);

  const report = buildReport(results);
  await writeFile('doc-audit-report.md', report);
  await writeFile('doc-audit-report.json', JSON.stringify(results, null, 2));

  const reruns = results.filter((r) => r.verdict === 'rerun').length;
  console.log(`\nAudited: ${results.length}/${files.length}`);
  console.log(`Flagged for re-run: ${reruns}`);
  console.log('Reports: doc-audit-report.md, doc-audit-report.json');
}
