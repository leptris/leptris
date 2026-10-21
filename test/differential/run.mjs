#!/usr/bin/env node
// Differential corpus runner (TypeScript/WASM binding, Node).
// Prints one canonical line per corpus doc:
//   docname|xpath_count|sha256(normalized_serialization)
// Normalization (see README.md): the xml() emit keeps the source prolog
// (declaration + DOCTYPE); it is stripped so comparison starts at the root.

import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { close, init, load, xml, xpath } from "leptris";

const CORPUS = join(dirname(fileURLToPath(import.meta.url)), "corpus");

// Fixed corpus order — keep byte-identical across run.rb / run.py / run.mjs.
const DOCS = [
  "basic.xml",
  "cdata.xml",
  "comments_pis.xml",
  "dtd_model.xml",
  "entities_dtd.xml",
  "mixed_deep.xml",
  "namespaces.xml",
  "xpath_target.xml",
];

const XPATHS = {
  "basic.xml": "count(//ns:elem)",
  "cdata.xml": "count(//text)",
  "comments_pis.xml": "count(//comment())",
  "dtd_model.xml": "count(//item[@sku])",
  "entities_dtd.xml": "count(//title)",
  "mixed_deep.xml": "count(//p/*)",
  "namespaces.xml": "count(//b:book)",
  "xpath_target.xml": "count(//order[@status='active']/item)",
};

// Root element start: first `<` followed by a name character. Every prolog
// node (declaration, DOCTYPE subset, leading comments/PIs) starts `<?` or `<!`.
const ROOT_START_RE = /<[A-Za-z_]/;

await init();
for (const name of DOCS) {
  const id = await load(readFileSync(join(CORPUS, name), "utf8"));
  const count = await xpath(id, XPATHS[name]);
  const full = await xml(id);
  const body = full.slice(full.search(ROOT_START_RE));
  const digest = createHash("sha256").update(body, "utf8").digest("hex");
  console.log(`${name}|${Math.trunc(Number(count))}|${digest}`);
  await close(id);
}
