/**
 * Header/source pairing for C++ files in the xrpld layout.
 *
 * libxrpl: src/libxrpl/<X>.cpp <-> include/xrpl/<X>.h
 * xrpld:   src/xrpld/<X>.cpp   <-> src/xrpld/<X>.h (same directory)
 *
 * Inline-only headers may have no .cpp partner; standalone .cpp may have
 * no .h partner.
 */

import { existsSync } from 'node:fs';
import { relative, resolve } from 'node:path';
import { XRPLD_ROOT } from './config.js';

/**
 * Compute the partner file path for a given primary, by swapping the
 * extension between header/source. Returns null if no candidate exists
 * on disk.
 */
export function findPartner(absPrimary: string): string | null {
  const rel = relative(XRPLD_ROOT, absPrimary);
  const dotIdx = rel.lastIndexOf('.');
  if (dotIdx === -1) return null;
  const stem = rel.slice(0, dotIdx);
  const ext = rel.slice(dotIdx);

  const candidates: string[] = [];

  if (ext === '.cpp') {
    if (stem.startsWith('src/libxrpl/')) {
      const tail = stem.slice('src/libxrpl/'.length);
      candidates.push(`include/xrpl/${tail}.h`, `include/xrpl/${tail}.hpp`);
    }
    candidates.push(`${stem}.h`, `${stem}.hpp`);
  } else if (ext === '.h' || ext === '.hpp') {
    if (stem.startsWith('include/xrpl/')) {
      candidates.push(`src/libxrpl/${stem.slice('include/xrpl/'.length)}.cpp`);
    }
    candidates.push(`${stem}.cpp`);
  }

  for (const candidate of candidates) {
    const abs = resolve(XRPLD_ROOT, candidate);
    if (existsSync(abs) && abs !== absPrimary) return abs;
  }
  return null;
}
