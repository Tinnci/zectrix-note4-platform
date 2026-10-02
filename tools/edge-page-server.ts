/** Dedicated Note4 page endpoint. Run behind an HTTPS reverse proxy. */
export const PAGE_BYTES = 15032;

export function encodePage(pbm: Uint8Array, revision: number, issued: number, expires: number, next: number): Uint8Array {
  if (!Number.isInteger(revision) || revision < 1 || revision > 0xffffffff ||
      !Number.isInteger(issued) || issued < 1704067200 || !Number.isInteger(expires) || expires <= issued ||
      expires - issued > 604800 || expires > 0xffffffff || !Number.isInteger(next) ||
      (next !== 0 && (next <= issued || next > expires))) throw new Error("Invalid page times or revision");
  // Strict canonical PBM input keeps this example bounded and predictable.
  const portraitHeader = new TextEncoder().encode("P4\n300 400\n");
  const landscapeHeader = new TextEncoder().encode("P4\n400 300\n");
  const equal = (a: Uint8Array, b: Uint8Array) => b.every((value, i) => a[i] === value);
  const portrait = equal(pbm, portraitHeader);
  const header = portrait ? portraitHeader : landscapeHeader;
  const width = portrait ? 300 : 400, height = portrait ? 400 : 300;
  const stride = Math.ceil(width / 8);
  if (!equal(pbm, header) || pbm.length !== header.length + stride * height) throw new Error("Expected canonical 300x400 or 400x300 P4 PBM");
  const output = new Uint8Array(PAGE_BYTES);
  output.fill(0xff, 32); // Native display polarity: one is white, zero is black.
  output.set(new TextEncoder().encode("ZEP1")); output[4] = portrait ? 1 : 0;
  const view = new DataView(output.buffer);
  view.setUint32(8, revision, true); view.setUint32(12, issued, true);
  view.setUint32(16, expires, true); view.setUint32(20, next, true);
  // PBM portrait rows have padding bits; firmware stores a tightly packed frame.
  for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
    if (pbm[header.length + y * stride + (x >> 3)]! & (0x80 >> (x & 7))) {
      const bit = y * width + x;
      output[32 + (bit >> 3)]! &= ~(0x80 >> (bit & 7));
    }
  }
  return output;
}

export function pageResponse(page: Uint8Array): Response {
  return new Response(new Uint8Array(page).buffer, { headers: {
    "Content-Type": "application/octet-stream", "Content-Length": String(page.length),
    "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff",
  } });
}

export function createPageHandler(load: () => Promise<Uint8Array>, token: string, clock = () => Math.floor(Date.now() / 1000), report?: (request: Request, page: Uint8Array) => Promise<void>, revision = (issued: number) => issued) {
  if (!/^[A-Za-z0-9_-]{32,64}$/.test(token)) throw new Error("Invalid read-only page token");
  return async (request: Request): Promise<Response> => {
    const url = new URL(request.url);
    if (request.method !== "GET" || url.pathname !== "/note4/page" || url.search) return new Response("Not found", { status: 404 });
    if (request.headers.get("Authorization") !== `Bearer ${token}`) return new Response("Unauthorized", { status: 401 });
    try {
      const pbm = await load();
      const issued = clock();
      const page = encodePage(pbm, revision(issued), issued, issued + 86400, issued + 21600);
      // Reporting failure must not suppress a valid page or leak credentials.
      if (report) { try { await report(request, page); } catch { /* Retry on the next request. */ } }
      return pageResponse(page);
    } catch {
      return new Response("Page unavailable", { status: 503 });
    }
  };
}

if (import.meta.main) {
  const filePath = Bun.argv[2];
  if (!filePath) throw new Error("Usage: bun tools/edge-page-server.ts <canonical-p4.pbm>");
  const port = Number(Bun.env.EDGE_PORT ?? "8787");
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("Invalid EDGE_PORT");
  const token = Bun.env.EDGE_TOKEN;
  if (!token || !/^[A-Za-z0-9_-]{32,64}$/.test(token)) throw new Error("EDGE_TOKEN must be a dedicated 32-64 character read-only token");
  const server = Bun.serve({ hostname: "127.0.0.1", port, fetch: createPageHandler(async () => {
      const file = Bun.file(filePath);
      if (file.size > 16000) throw new Error("PBM too large");
      return new Uint8Array(await file.arrayBuffer());
    }, token) });
  console.log(`Note4 page endpoint: http://127.0.0.1:${server.port}/note4/page (HTTPS proxy required)`);
}
