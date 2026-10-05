/** Verify generated native calibration pages through the real page encoder. */
import { encodePage, PAGE_BYTES } from "./edge-page-server";

const directory = Bun.argv[2];
if (!directory) throw new Error("Usage: bun tools/cjk_page_test.ts <evaluation-directory>");
let count = 0;
for await (const name of new Bun.Glob("panel-T*.pbm").scan({ cwd: directory })) {
  const pbm = new Uint8Array(await Bun.file(`${directory}/${name}`).arrayBuffer());
  const portrait = new TextDecoder().decode(pbm.subarray(0, 11)) === "P4\n300 400\n";
  const width = portrait ? 300 : 400, height = portrait ? 400 : 300;
  const page = encodePage(pbm, 1, 1704067200, 1704153600, 0);
  if (page.length !== PAGE_BYTES || page[4] !== (portrait ? 1 : 0)) throw new Error(`Invalid geometry: ${name}`);
  for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
    const black = (pbm[11 + y * Math.ceil(width / 8) + (x >> 3)]! >> (7 - (x & 7))) & 1;
    const offset = y * width + x;
    const white = (page[32 + (offset >> 3)]! >> (7 - (offset & 7))) & 1;
    if (black === white) throw new Error(`Pixel polarity/packing mismatch: ${name} (${x}, ${y})`);
  }
  ++count;
}
const report = await Bun.file(`${directory}/report.json`).json();
if (count !== report.panel_trials.length) throw new Error(`Expected ${report.panel_trials.length} role/method/orientation pages, received ${count}`);
console.log(`PASS: ${count} native pages encoded pixel-for-pixel with correct polarity and portrait row padding.`);
