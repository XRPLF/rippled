/**
 * Loads system prompts and injects module-specific skill context.
 */

import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { PROMPTS_DIR, SKILLS_DIR, skillForPath } from './config.js';

/**
 * Load a system prompt from prompts/ and append the relevant module skill
 * if one applies to the given source path.
 *
 * @param promptName - Base name of the prompt file (without .md extension)
 * @param sourcePath - Path relative to the xrpld repo root
 * @returns The fully-assembled system prompt
 */
export async function loadSystemPrompt(promptName: string, sourcePath: string): Promise<string> {
  const basePromptPath = resolve(PROMPTS_DIR, `${promptName}.md`);
  const basePrompt = await readFile(basePromptPath, 'utf8');

  const skillFile = skillForPath(sourcePath);
  if (skillFile === null) {
    return basePrompt;
  }

  const skillPath = resolve(SKILLS_DIR, 'soul', skillFile);
  if (!existsSync(skillPath)) {
    return basePrompt;
  }

  const skill = await readFile(skillPath, 'utf8');
  return `${basePrompt}\n\n## Module Skill (${skillFile})\n\n${skill}`;
}
