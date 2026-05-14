/**
 * Document mode: add Doxygen docs to a file or all files in a directory.
 */

import { existsSync, readdirSync, statSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { join, relative, resolve } from 'node:path';
import { query } from '@anthropic-ai/claude-agent-sdk';
import { MODEL, XRPLD_ROOT } from './config.js';
import { findPartner } from './pairing.js';
import { loadSystemPrompt } from './prompt-loader.js';

const CPP_EXTENSIONS: ReadonlySet<string> = new Set(['.h', '.hpp', '.cpp']);

/**
 * Recursively find all C++ source files under a target path.
 *
 * @param target - File or directory path (relative to xrpld root or absolute)
 * @returns Absolute paths of all matching files
 */
function findCppFiles(target: string): string[] {
  const absTarget = resolve(XRPLD_ROOT, target);
  if (!existsSync(absTarget)) {
    throw new Error(`Target does not exist: ${absTarget}`);
  }

  const stat = statSync(absTarget);
  if (stat.isFile()) {
    return [absTarget];
  }

  const results: string[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (entry.isFile()) {
        const dotIdx = entry.name.lastIndexOf('.');
        if (dotIdx === -1) continue;
        const ext = entry.name.slice(dotIdx);
        if (CPP_EXTENSIONS.has(ext)) {
          results.push(full);
        }
      }
    }
  };
  walk(absTarget);
  return results;
}

/**
 * Read the sibling .ai.md file for a source file, if one exists.
 *
 * The athenah-ai pipeline produces a `<file>.ai.md` companion for every
 * documented source file (e.g., `Slice.h` -> `Slice.h.ai.md`). When present,
 * it is high-signal prose describing the file's purpose, design, and
 * non-obvious behavior — the agent should use it as the authoritative
 * source of intent.
 */
async function readAiContext(absPath: string): Promise<string | null> {
  const aiPath = `${absPath}.ai.md`;
  if (!existsSync(aiPath)) return null;
  return await readFile(aiPath, 'utf8');
}

/**
 * Document a single file by running the documentation agent against it.
 *
 * Inject the partner file's path + its `.ai.md` (if any) into the prompt
 * so the agent can apply the "contract on header, implementation on
 * source" policy with full visibility into the other half. The agent
 * Reads the partner only as reference; only the primary file is edited.
 */
async function documentFile(absPath: string): Promise<void> {
  const relPath = relative(XRPLD_ROOT, absPath);
  console.log(`\n=== Documenting: ${relPath} ===`);

  const systemPrompt = await loadSystemPrompt('document-file', relPath);
  const aiContext = await readAiContext(absPath);
  const aiContextBlock =
    aiContext === null
      ? ''
      : `\n\n## Primary's Authoritative AI Context (${relPath}.ai.md)\n\nThe following is high-signal prose describing this file's purpose, design,\nand non-obvious behavior. Treat it as the source of truth for intent and\nbehavior. Your job is to translate this into structured Doxygen \`/** */\`\ncomments on the actual declarations.\n\n---\n\n${aiContext}\n---`;

  const absPartner = findPartner(absPath);
  const relPartner = absPartner === null ? null : relative(XRPLD_ROOT, absPartner);
  const partnerAiContext = absPartner === null ? null : await readAiContext(absPartner);
  const partnerBlock =
    relPartner === null
      ? ''
      : `\n\n## Partner File\n\nThis file's partner is **${relPartner}**. Use the Read tool to see its\ncurrent docstrings before deciding what belongs on the primary. A concept\nalready documented on the partner does not need to be duplicated here.\nConversely, an implementation-depth concept currently on the partner that\nbelongs on the source (or vice versa) should be moved.${
          partnerAiContext === null
            ? ''
            : `\n\n### Partner's Authoritative AI Context (${relPartner}.ai.md)\n\n---\n\n${partnerAiContext}\n---`
        }`;

  const userPrompt = `Add Doxygen documentation to: ${relPath}

The file is rooted at ${XRPLD_ROOT}. Use the Read tool to read it, the Edit
tool to add documentation, and Glob/Grep to find related tests or callers
when needed.${
    relPartner === null
      ? ''
      : ` Use Read on the partner file (${relPartner}) to see what's already
documented there.`
  }

Do not modify any code logic — only add documentation comments to the
primary file (${relPath}). Do NOT edit the partner file.${aiContextBlock}${partnerBlock}`;

  const result = query({
    prompt: userPrompt,
    options: {
      model: MODEL,
      systemPrompt,
      cwd: XRPLD_ROOT,
      allowedTools: ['Read', 'Edit', 'Glob', 'Grep', 'Bash'],
      permissionMode: 'acceptEdits',
    },
  });

  for await (const message of result) {
    if (message.type === 'assistant') {
      const content = message.message?.content;
      if (Array.isArray(content)) {
        for (const block of content) {
          if (block.type === 'text') {
            process.stdout.write(block.text);
          }
        }
      }
    }
    if (message.type === 'result') {
      const cost = message.total_cost_usd?.toFixed(4) ?? '?';
      const inTok = message.usage?.['input_tokens'] ?? 0;
      const outTok = message.usage?.['output_tokens'] ?? 0;
      console.log(`\n[Cost: $${cost}, Tokens: ${inTok}/${outTok}]`);
    }
  }
}

/**
 * Document a file or every C++ file under a directory.
 *
 * @param target - File or directory path
 */
export async function documentTarget(target: string): Promise<void> {
  const files = findCppFiles(target);
  console.log(`Found ${files.length} C++ file(s) to document.`);

  for (const file of files) {
    try {
      await documentFile(file);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      console.error(`Failed to document ${file}: ${message}`);
    }
  }
}
