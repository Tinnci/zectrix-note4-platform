/** Native framebuffer previews; no HA credentials, hardware or browser runtime required. */
import { copyFile, mkdir, mkdtemp, readdir, rename, rm } from "node:fs/promises";
import { resolve } from "node:path";
import { renderStates } from "./ha-bridge/renderer";

export function pbmToSvg(pbm: Uint8Array): { width: number; height: number; svg: string } {
  const prefix = new TextDecoder().decode(pbm.subarray(0, 32));
  const match = /^P4\n(300 400|400 300)\n/.exec(prefix);
  if (!match) throw new Error("Expected native Note4 P4 PBM");
  const [width, height] = match[1]!.split(" ").map(Number) as [number, number];
  const stride = Math.ceil(width / 8), offset = match[0].length;
  if (pbm.length !== offset + stride * height) throw new Error("Invalid PBM length");
  let path = "";
  for (let y = 0; y < height; y++) {
    let start = -1;
    for (let x = 0; x <= width; x++) {
      const black = x < width && !!(pbm[offset + y * stride + (x >> 3)]! & (0x80 >> (x & 7)));
      if (black && start < 0) start = x;
      if (!black && start >= 0) { path += `M${start} ${y}h${x-start}v1H${start}z`; start = -1; }
    }
  }
  return { width, height, svg: `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" shape-rendering="crispEdges"><rect width="100%" height="100%" fill="white"/><path d="${path}" fill="black"/></svg>` };
}

export function obsoletePreviewFiles(existing: string[], fresh: Set<string>, previous: unknown): string[] {
  const managed = new Set(["overview.png", "status-icons.pbm", "zh-status-icons.pbm"]);
  if (Array.isArray(previous)) for (const entry of previous) {
    if (entry && typeof entry.name === "string" && /^[a-z0-9-]+$/.test(entry.name)) {
      managed.add(`${entry.name}.pbm`); managed.add(`${entry.name}.svg`);
    }
  }
  return existing.filter(file => managed.has(file) && !fresh.has(file));
}

if (import.meta.main) {
  const root = resolve(import.meta.dir, ".."), output = resolve(root, "build-ui-preview");
  const args = Bun.argv.slice(2);
  if (args.some(arg => arg !== "--reuse" && arg !== "--docs")) throw new Error("Usage: bun tools/ui-preview.ts [--reuse] [--docs]");
  const magick = args.includes("--docs") ? Bun.which("magick") : null;
  if (args.includes("--docs") && !magick) throw new Error("Documentation PNG export requires ImageMagick (magick)");
  await mkdir(output, { recursive: true });
  if (args.includes("--reuse") && !await Bun.file(`${output}/launcher.pbm`).exists())
    throw new Error("No firmware previews to reuse; run without --reuse first");
  // A removed test scene must not survive merely because its old PBM is on disk.
  const source = args.includes("--reuse") ? output : await mkdtemp(`${root}/build-ui-stage-`);
  if (!args.includes("--reuse")) {
    const process = Bun.spawn(["bash", "tools/test-display-service.sh"], {
      cwd: root, env: { ...Bun.env, ZECTRIX_UI_PREVIEW_DIR: source }, stdout: "inherit", stderr: "inherit",
    });
    if (await process.exited !== 0) throw new Error("Device UI tests failed; preview generation stopped");
  }
  const font = new Uint8Array(await Bun.file(`${root}/components/zectrix_reader/font/reader_font.bin`).arrayBuffer());
  for (const language of ["en", "zh"] as const) for (const orientation of ["portrait", "landscape"] as const) {
    const states = Array.from({ length: 8 }, (_, i) => ({ entity_id: `sensor.example_${i}`, state: String(20+i),
      attributes: { friendly_name: language === "zh" ? `房间 ${i+1} 温度` : `Room ${i+1} temperature`, unit_of_measurement: "°C" } }));
    await Bun.write(`${source}/${language === "zh" ? "zh-" : ""}ha-${orientation}.pbm`, renderStates(font, states, "2026-10-03 12:00 UTC", orientation));
  }
  const entries = [];
  for (const file of (await readdir(source)).filter(name => /^[a-z0-9-]+\.pbm$/.test(name)).sort()) {
    // The host suite also exports an 800×680 icon contact sheet, not a device frame.
    if (file === "status-icons.pbm" || file === "zh-status-icons.pbm") continue;
    const bitmap = pbmToSvg(new Uint8Array(await Bun.file(`${source}/${file}`).arrayBuffer()));
    const name = file.slice(0, -4), svgFile = `${name}.svg`;
    await Bun.write(`${source}/${svgFile}`, bitmap.svg);
    entries.push({ name, file: svgFile, width: bitmap.width, height: bitmap.height,
      orientation: bitmap.width === 300 ? "portrait" : "landscape", language: name.startsWith("zh-") ? "zh" : "en",
      source: name.replace(/^zh-/, "").startsWith("ha-") ? "server" : "firmware" });
  }
  if (source !== output) {
    const fresh = new Set(await readdir(source));
    const previousFile = Bun.file(`${output}/manifest.json`);
    const previous: unknown = await previousFile.exists() ? await previousFile.json() : [];
    const stale = obsoletePreviewFiles(await readdir(output), fresh, previous);
    if (stale.length) {
      const archive = await mkdtemp(`${root}/build-ui-retired-`);
      for (const file of stale) await rename(`${output}/${file}`, `${archive}/${file}`);
      console.log(`Retired ${stale.length} obsolete preview files to ${archive} (recoverable)`);
    }
    for (const file of fresh) await copyFile(`${source}/${file}`, `${output}/${file}`);
  }
  if (magick) {
    const screenshots = resolve(root, "docs/screenshots");
    const mapping = { home: "home-full", reader: "reader-rich-small", settings: "settings",
      book_transfer: "books-mode", calendar: "utilities-calendar-today", sleep_dashboard: "sleep-dashboard",
      sleep_portrait: "sleep-portrait" };
    for (const [name, scene] of Object.entries(mapping)) for (const language of ["en", "zh"]) {
      const pbm = `${output}/${language === "zh" ? "zh-" : ""}${scene}.pbm`;
      const conversion = Bun.spawn([magick, pbm, "-strip", `${screenshots}/${name}_${language}.png`], { stdout: "inherit", stderr: "inherit" });
      if (await conversion.exited !== 0) throw new Error("Documentation image conversion failed");
    }
    const icons = Bun.spawn([magick, `${output}/zh-status-icons.pbm`, "-strip", `${screenshots}/status_bar_icons.png`], { stdout: "inherit", stderr: "inherit" });
    if (await icons.exited !== 0) throw new Error("Documentation icon conversion failed");
  }
  await Bun.write(`${output}/manifest.json`, JSON.stringify(entries, null, 2));
  await Bun.write(`${output}/index.html`, `<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Note4 UI 预览</title>
<style>body{font:16px system-ui;margin:24px;background:#eee;color:#222}header{position:sticky;top:0;background:#eee;padding:12px 0}label{margin-right:16px}select{font:inherit}main{display:flex;align-items:flex-start;gap:24px;flex-wrap:wrap}figure{margin:0}img{display:block;border:1px solid #bbb;image-rendering:pixelated;max-width:none}figcaption{max-width:400px;margin:8px 0 24px}small{display:block}</style>
<header><h1>Note4 原生界面预览</h1><p>横屏 400×300 · 竖屏 300×400。竖屏支持日历与远程页面，应用/阅读器未重排。预览不代表实机刷新效果。</p>
<label>方向 <select id="orientation"><option value="">全部</option><option>landscape</option><option>portrait</option></select></label>
<label>语言 <select id="language"><option value="">全部</option><option>zh</option><option>en</option></select></label>
<label>来源 <select id="source"><option value="">全部</option><option value="firmware">固件</option><option value="server">HA 示例</option></select></label>
<label>查找 <input id="search" type="search" placeholder="sleep / reader / connectivity"></label>
<label>缩放 <select id="zoom"><option value="1">1×</option><option value="2">2×</option><option value="3">3×</option></select></label></header>
<main>${entries.map(e => `<figure data-name="${e.name}" data-source="${e.source}" data-orientation="${e.orientation}" data-language="${e.language}"><a href="${e.file}"><img src="${e.file}" width="${e.width}" height="${e.height}" alt="${e.name}" data-width="${e.width}" data-height="${e.height}"></a><figcaption>${e.name}<small>${e.source} · ${e.width}×${e.height} · ${e.language} · <a href="${e.name}.pbm">PBM</a></small></figcaption></figure>`).join("")}</main>
<script>const orientation=document.querySelector('#orientation'),language=document.querySelector('#language'),source=document.querySelector('#source'),search=document.querySelector('#search'),zoom=document.querySelector('#zoom');function update(){for(const f of document.querySelectorAll('figure'))f.hidden=!!((orientation.value&&f.dataset.orientation!==orientation.value)||(language.value&&f.dataset.language!==language.value)||(source.value&&f.dataset.source!==source.value)||!f.dataset.name.includes(search.value.trim().toLowerCase()));for(const i of document.querySelectorAll('img')){i.width=Number(i.dataset.width)*Number(zoom.value);i.height=Number(i.dataset.height)*Number(zoom.value)}}for(const control of [orientation,language,source,zoom])control.addEventListener('change',update);search.addEventListener('input',update);</script></html>`);
  console.log(`Preview: ${output}/index.html (${entries.length} screens)`);
  if (source !== output) await rm(source, { recursive: true });
}
