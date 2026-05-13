/**
 * Regen-skills mode: rebuild a module's skill file from ai.md inputs.
 *
 * For a given module (e.g. `protocol`, `ledger`, `consensus`), collect all
 * `.ai.md` files under the matching source paths and ask the agent to
 * produce an updated `docs/skills/<module>.md`.
 */

import { existsSync, readdirSync, statSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';
import { join, relative, resolve } from 'node:path';
import { query } from '@anthropic-ai/claude-agent-sdk';
import { MODEL, MODULE_SKILL_MAP, PROMPTS_DIR, SKILLS_DIR, XRPLD_ROOT } from './config.js';

interface AiFile {
  readonly sourcePath: string;
  readonly content: string;
}

/** Resolve which source-tree prefixes feed a given skill file. */
function prefixesForSkill(skillFile: string): string[] {
  return Object.entries(MODULE_SKILL_MAP)
    .filter(([, mapped]) => mapped === skillFile)
    .map(([prefix]) => prefix);
}

/** Walk a directory and collect all sibling .ai.md files. */
function collectAiFiles(prefix: string): string[] {
  const absDir = resolve(XRPLD_ROOT, prefix);
  if (!existsSync(absDir) || !statSync(absDir).isDirectory()) return [];

  const results: string[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (entry.isFile() && entry.name.endsWith('.ai.md')) {
        results.push(full);
      }
    }
  };
  walk(absDir);
  return results;
}

async function loadAiFiles(absPaths: readonly string[]): Promise<AiFile[]> {
  const files: AiFile[] = [];
  for (const absPath of absPaths) {
    const content = await readFile(absPath, 'utf8');
    files.push({
      sourcePath: relative(XRPLD_ROOT, absPath).replace(/\.ai\.md$/, ''),
      content,
    });
  }
  return files;
}

/**
 * Regenerate the skill file for a given module name.
 *
 * @param moduleName - The skill file name without extension (e.g. "protocol",
 *   "ledger"). Must match a key in the MODULE_SKILL_MAP value set.
 */
export async function regenSkills(moduleName: string): Promise<void> {
  const skillFile = `${moduleName}.md`;
  const prefixes = prefixesForSkill(skillFile);

  if (prefixes.length === 0) {
    throw new Error(
      `Unknown module: ${moduleName}. Valid modules: ${Array.from(new Set(Object.values(MODULE_SKILL_MAP).filter((v): v is string => v !== null))).join(', ')}`,
    );
  }

  console.log(`Regenerating skill: ${skillFile}`);
  console.log(`  Source prefixes: ${prefixes.join(', ')}`);

  const aiPaths = prefixes.flatMap((prefix) => collectAiFiles(prefix));
  if (aiPaths.length === 0) {
    console.warn('  No .ai.md files found for this module. Skipping.');
    return;
  }
  console.log(`  Found ${aiPaths.length} .ai.md file(s)`);

  const aiFiles = await loadAiFiles(aiPaths);
  const skillPath = resolve(SKILLS_DIR, skillFile);
  const existingSkill = existsSync(skillPath)
    ? await readFile(skillPath, 'utf8')
    : '(no existing skill file — create a new one)';

  const systemPrompt = await readFile(resolve(PROMPTS_DIR, 'regen-skill.md'), 'utf8');

  const aiBlocks = aiFiles
    .map((f) => `\n### \`${f.sourcePath}\`\n\n${f.content}`)
    .join('\n\n---\n');

  const userPrompt = `Regenerate the skill file: \`docs/skills/${skillFile}\`

## Existing skill content

${existingSkill}

## AI context files for this module

${aiBlocks}

Produce the new complete skill file content as your final message.`;

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
          if (block.type === 'text') {
            response += block.text;
          }
        }
      }
    }
    if (message.type === 'result') {
      const cost = message.total_cost_usd?.toFixed(4) ?? '?';
      console.log(`  [Cost: $${cost}]`);
    }
  }

  const trimmed = response.trim();
  if (trimmed.length === 0) {
    console.error('  Agent returned empty response — skill file not updated.');
    return;
  }

  await writeFile(skillPath, `${trimmed}\n`);
  console.log(`  Wrote: ${relative(XRPLD_ROOT, skillPath)}`);
}
