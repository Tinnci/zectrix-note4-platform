import { mkdir, readdir } from 'node:fs/promises';

const [binary, root] = Bun.argv.slice(2);
const code = 'ABCDEFGH2345';
const assert = (condition, message) => { if (!condition) throw new Error(message); };
await mkdir(root);
for (let i = 0; i < 40; i++) await Bun.write(`${root}/A${String(i).padStart(3, '0')}.txt`, 'A book.');

async function start() {
  const child = Bun.spawn([binary, root], { stdin: 'ignore', stdout: 'pipe', stderr: 'pipe' });
  const timer = setTimeout(() => child.kill(), 20000);
  const reader = child.stdout.getReader();
  let output = '';
  while (!output.includes('\n')) {
    const { value, done } = await reader.read();
    assert(!done, 'The Host HTTP server failed to start.');
    output += new TextDecoder().decode(value);
  }
  clearTimeout(timer);
  const url = output.match(/READY (http:\/\/127\.0\.0\.1:\d+\/)/)?.[1];
  assert(url, output);
  return { child, reader, url };
}

let server;
try {
  server = await start();
  const request = (path, options = {}, authorized = true) => fetch(server.url + path, {
    ...options, headers: authorized ? { Authorization: `Bearer ${code}` } : {},
    signal: AbortSignal.timeout(10000),
  });
  const page = await request('', {}, false);
  assert(page.ok && (await page.text()).includes('Upload &amp; finish'), 'The actual web page was not served.');
  assert(page.headers.get('x-content-type-options') === 'nosniff', 'Missing content type protection.');
  assert(!page.headers.has('access-control-allow-origin'), 'Book API must not allow cross-origin requests.');
  for (const method of ['GET', 'PUT', 'DELETE']) {
    const response = await request('api/books/new.txt', { method, ...(method === 'PUT' ? { body: 'reject' } : {}) }, false);
    assert(response.status === 401, `Unauthenticated ${method} was accepted.`);
    await response.text();
  }
  let response = await request('api/books');
  let listing = await response.json();
  assert(listing.files.length === 32 && listing.more && listing.files[31].name === 'A031.txt', 'First library page is wrong.');
  listing = await (await request('api/books?after=A031.txt')).json();
  assert(listing.files.length === 8 && !listing.more, 'Second library page is wrong.');

  const name = '书"&稿.epub';
  const body = Uint8Array.from({ length: 131072 }, (_, i) => i % 256);
  response = await request(`api/books/${encodeURIComponent(name)}`, { method: 'PUT', body });
  assert(response.ok && (await response.json()).ok, 'Binary EPUB upload failed.');
  response = await request(`api/books/${encodeURIComponent(name)}`);
  const downloaded = new Uint8Array(await response.arrayBuffer());
  assert(downloaded.length === body.length && downloaded.every((value, i) => value === body[i]), 'Download changed binary bytes.');
  assert(response.headers.get('content-disposition')?.startsWith("attachment; filename*=UTF-8''"), 'Missing download filename.');
  response = await request(`api/books/${encodeURIComponent(name)}`, { method: 'PUT', body: 'do not replace' });
  assert(response.status === 409, 'Duplicate upload replaced a book.');
  await response.text();

  const address = new URL(server.url);
  await new Promise((resolve, reject) => {
    Bun.connect({ hostname: '127.0.0.1', port: Number(address.port), socket: {
      open(socket) { socket.end(`PUT /api/books/interrupted.txt HTTP/1.1\r\nHost: ${address.host}\r\nAuthorization: Bearer ${code}\r\nContent-Length: 10000\r\n\r\npartial`); },
      data() {}, close() { resolve(); }, error(socket, error) { reject(error); },
    } }).catch(reject);
  });
  response = await request('api/books/interrupted.txt');
  assert(response.status === 404, 'A disconnected upload became visible.');
  await response.text();
  assert(!(await readdir(root)).includes('.upload.part'), 'Interrupted staging data was retained.');
  response = await request('api/books/interrupted.txt', { method: 'PUT', body: '完整的一本书。' });
  assert(response.ok, 'Retry after disconnect failed.'); await response.text();
  response = await request(`api/books/${encodeURIComponent(name)}`, { method: 'DELETE' });
  assert(response.ok, 'Delete failed.'); await response.text();
  response = await request('api/books/%2e%2e%2foutside.txt', { method: 'PUT', body: 'escape' });
  assert(response.status === 400, 'Path traversal was accepted.'); await response.text();
  response = await request('api/finish', { method: 'POST' });
  assert(response.ok && (await response.json()).ok, 'Session completion was not acknowledged.');
  const timeout = setTimeout(() => server.child.kill(), 5000);
  assert(await server.child.exited === 0, await new Response(server.child.stderr).text());
  clearTimeout(timeout);
  assert((await Bun.file(`${root}/interrupted.txt`).text()) === '完整的一本书。', 'Completed upload was not persisted.');
  for (const phase of ['headers', 'body']) {
    server = await start();
    const address = new URL(server.url);
    const socket = await Bun.connect({ hostname: '127.0.0.1', port: Number(address.port), socket: {
      open(socket) {
        socket.write(`PUT /api/books/stalled.txt HTTP/1.1\r\nHost: ${address.host}\r\n` +
          (phase === 'body' ? `Authorization: Bearer ${code}\r\nContent-Length: 10000\r\n\r\npartial` : 'X-Slow: '));
      }, data() {}, close() {}, error() {},
    } });
    await Bun.sleep(100);
    const started = performance.now();
    server.child.kill('SIGTERM');
    const kill = setTimeout(() => server.child.kill('SIGKILL'), 3000);
    assert(await server.child.exited === 0, `Shutdown stalled while reading ${phase}.`);
    clearTimeout(kill);
    assert(performance.now() - started < 1500, `Shutdown failed to interrupt ${phase}.`);
    socket.end();
    assert(!(await Bun.file(`${root}/stalled.txt`).exists()) && !(await Bun.file(`${root}/.upload.part`).exists()), 'Shutdown left partial data.');
  }
  console.log('PASS: real HTTP upload/download, authorization, pagination, disconnect recovery and automatic session shutdown.');
} finally {
  if (server?.child.exitCode === null) server.child.kill();
}
