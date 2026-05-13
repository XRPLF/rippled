/**
 * Shared configuration for doc-agent.
 *
 * Paths are resolved relative to the doc-agent directory so the tool works
 * regardless of where it's invoked from.
 */

import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/** Absolute path to the doc-agent root (parent of src/). */
export const AGENT_DIR: string = resolve(__dirname, '..');

/** Absolute path to the prompts directory. */
export const PROMPTS_DIR: string = resolve(AGENT_DIR, 'prompts');

/**
 * Absolute path to the xrpld repo root.
 *
 * Defaults to three levels up from doc-agent (which lives at
 * .github/scripts/doc-agent/). Override with the XRPLD_ROOT env var when
 * running against a different checkout.
 */
export const XRPLD_ROOT: string = process.env['XRPLD_ROOT'] ?? resolve(AGENT_DIR, '..', '..', '..');

/** Model used for documentation generation and review. */
export const MODEL: string = process.env['DOC_AGENT_MODEL'] ?? 'claude-sonnet-4-6';

/** Absolute path to the skills directory inside the xrpld repo. */
export const SKILLS_DIR: string = resolve(XRPLD_ROOT, 'docs', 'skills');

/**
 * Map module path prefixes to their skill file name in docs/skills/soul/.
 *
 * Used to inject module-specific context into the agent's system prompt
 * when documenting or reviewing code in that module.
 */
export const MODULE_SKILL_MAP: Readonly<Record<string, string | null>> = {
  'src/libxrpl/basics/': null,
  'src/libxrpl/crypto/': 'cryptography.md',
  'src/libxrpl/json/': null,
  'src/libxrpl/beast/': null,
  'src/libxrpl/protocol/': 'protocol.md',
  'src/libxrpl/ledger/': 'ledger.md',
  'src/libxrpl/tx/': 'transactors.md',
  'src/libxrpl/nodestore/': 'nodestore.md',
  'src/libxrpl/shamap/': 'shamap.md',
  'src/libxrpl/rdb/': 'sql.md',
  'src/xrpld/consensus/': 'consensus.md',
  'src/xrpld/overlay/': 'peering.md',
  'src/xrpld/peerfinder/': 'peering.md',
  'src/xrpld/rpc/': 'rpc.md',
  'include/xrpl/crypto/': 'cryptography.md',
  'include/xrpl/protocol/': 'protocol.md',
  'include/xrpl/ledger/': 'ledger.md',
  'include/xrpl/tx/': 'transactors.md',
  'include/xrpl/nodestore/': 'nodestore.md',
  'include/xrpl/shamap/': 'shamap.md',
};

/**
 * Resolve which skill file applies to a given source path.
 *
 * @param sourcePath - Path relative to the xrpld repo root
 * @returns The skill file name, or null if no skill applies
 */
export function skillForPath(sourcePath: string): string | null {
  for (const [prefix, skillFile] of Object.entries(MODULE_SKILL_MAP)) {
    if (sourcePath.startsWith(prefix) || sourcePath.includes(`/${prefix}`)) {
      return skillFile;
    }
  }
  return null;
}
