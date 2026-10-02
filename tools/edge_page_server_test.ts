import { expect, test } from "bun:test";
import { encodePage, pageResponse, PAGE_BYTES, createPageHandler } from "./edge-page-server";
const now = 1704067200;
test("portrait PBM row padding is removed, including unaligned rows", () => {
  const header = new TextEncoder().encode("P4\n300 400\n");
  const pbm = new Uint8Array(header.length + 38 * 400); pbm.set(header);
  pbm[header.length] = 0x80;
  pbm[header.length + 38] = 0x80;
  pbm[header.length + 37] = 0x0f; // Padding bits must not leak into next row.
  const page = encodePage(pbm, 1, now, now + 86400, now + 3600);
  expect(page.length).toBe(PAGE_BYTES); expect(page[4]).toBe(1);
  expect(page[32]).toBe(0x7f); expect(page[32 + (300 >> 3)]).toBe(0xf7);
  expect(new DataView(page.buffer).getUint32(8, true)).toBe(1);
  expect(new DataView(page.buffer).getUint32(24, true)).toBe(0);
});
test("real HTTP endpoint authenticates, rejects unknown routes, and serves bounded pages", async () => {
  const header = new TextEncoder().encode("P4\n400 300\n");
  const pbm = new Uint8Array(header.length + 15000); pbm.set(header);
  const token = "0123456789abcdef0123456789abcdef";
  let fail = false;
  const server = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: createPageHandler(async () => {
    if (fail) throw new Error("Renderer unavailable");
    return pbm;
  }, token, () => now) });
  try {
    const url = `http://127.0.0.1:${server.port}/note4/page`;
    expect((await fetch(url)).status).toBe(401);
    expect((await fetch(url, { headers: { Authorization: "Bearer wrong" } })).status).toBe(401);
    const options = { headers: { Authorization: `Bearer ${token}` } };
    const response = await fetch(url, options);
    expect(response.status).toBe(200); expect(response.headers.get("Content-Length")).toBe(String(PAGE_BYTES));
    expect((await response.arrayBuffer()).byteLength).toBe(PAGE_BYTES);
    expect((await fetch(`${url}?token=${token}`, options)).status).toBe(404);
    expect((await fetch(url, { ...options, method: "POST" })).status).toBe(404);
    fail = true; expect((await fetch(url, options)).status).toBe(503);
  } finally { await server.stop(true); }
});
test("landscape, endpoint headers, and invalid input", async () => {
  const header = new TextEncoder().encode("P4\n400 300\n");
  const pbm = new Uint8Array(header.length + 15000); pbm.set(header); pbm[pbm.length - 1] = 1;
  const page = encodePage(pbm, 9, now, now + 3600, 0);
  expect(page[4]).toBe(0); expect(page[page.length - 1]).toBe(0xfe);
  const response = pageResponse(page);
  expect(response.headers.get("Content-Type")).toBe("application/octet-stream");
  expect((await response.arrayBuffer()).byteLength).toBe(PAGE_BYTES);
  expect(() => encodePage(pbm.subarray(1), 1, now, now + 3600, 0)).toThrow();
  expect(() => encodePage(pbm, 0, now, now + 3600, 0)).toThrow();
  expect(() => encodePage(pbm, 1, now, now + 3600, now + 4000)).toThrow();
});
