// Run after build.bat: node test/relative-images.mjs
// Exercises the real WebView2 host with an isolated browser profile.
import { spawn } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';

const root = path.resolve(import.meta.dirname, '..');
const work = path.join(root, 'build', `relative-images-${Date.now()}`);
for (const [folder, width] of [['', 60], ['sub', 120]]) {
  const dir = path.join(work, folder);
  await mkdir(path.join(dir, 'imgs'), { recursive: true });
  await writeFile(path.join(dir, 'imgs', 'img1.svg'),
    `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="30"><rect width="100%" height="100%" fill="green"/></svg>`);
  await writeFile(path.join(dir, 'Readme.md'),
    `# Relative images ${width}\n\n![Image](imgs/img1.svg)\n\n![Dot path](./imgs/img1.svg)\n\n<img src="imgs/img1.svg">\n\n[Subfolder](sub/Readme.md)\n`);
}
const port = 19223;
const app = spawn(path.join(root, 'MarkdownViewer.exe'), [path.join(work, 'Readme.md')], {
  cwd: root, windowsHide: true,
  env: { ...process.env, WEBVIEW2_USER_DATA_FOLDER: path.join(work, 'profile'),
    WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port}` },
});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let ws;
try {
  let target;
  for (let i = 0; i < 100 && !target; i++) {
    try {
      const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      target = pages.find(p => p.type === 'page' && p.url.includes('viewer.assets'));
    } catch {}
    if (!target) await delay(200);
  }
  assert.ok(target, 'WebView2 debugging target available');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 0;
  const pending = new Map();
  ws.onmessage = event => {
    const data = JSON.parse(event.data);
    if (pending.has(data.id)) {
      const { resolve, reject } = pending.get(data.id);
      pending.delete(data.id);
      data.error ? reject(new Error(JSON.stringify(data.error))) : resolve(data.result);
    }
  };
  const evaluate = expression => new Promise((resolve, reject) => {
    const id = ++nextId;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method: 'Runtime.evaluate', params: { expression, returnByValue: true } }));
  });
  async function checkImages(width) {
    let result;
    for (let i = 0; i < 100; i++) {
      try {
        result = (await evaluate(`Array.from(document.querySelectorAll('#content img'), i => ({width:i.naturalWidth, complete:i.complete, src:i.src}))`)).result.value;
        if (result?.length === 3 && result.every(i => i.complete && i.width === width)) break;
      } catch {}
      await delay(100);
    }
    assert.equal(result?.length, 3);
    assert.ok(result.every(i => i.complete && i.width === width), JSON.stringify(result));
  }
  await checkImages(60);
  console.log('PASS: initial open, relative path, ./ path, raw HTML image');
  await evaluate(`document.querySelector('#content a').click()`);
  await checkImages(120);
  console.log('PASS: new folder uses its own image at the same relative URL');
  await evaluate(`window.chrome.webview.postMessage('back')`);
  await checkImages(60);
  console.log('PASS: back navigation restores original folder images');
  await evaluate(`window.chrome.webview.postMessage('forward')`);
  await checkImages(120);
  console.log('PASS: forward navigation restores subfolder images');
  const sample = await readFile(path.join(root, 'test', 'sample.md'), 'utf8');
  const message = JSON.stringify({ type: 'render', name: 'sample.md', path: 'sample.md', markdown: sample });
  await evaluate(`window.chrome.webview.dispatchEvent(new MessageEvent('message', {data:${message}}))`);
  let smoke;
  for (let i = 0; i < 100; i++) {
    smoke = (await evaluate(`({
      heading: !!document.querySelector('#content h1'),
      table: !!document.querySelector('#content table'),
      checkbox: !!document.querySelector('#content input[type=checkbox]'),
      highlight: !!document.querySelector('#content code .hljs-keyword'),
      diagram: !!document.querySelector('#content .mermaid svg'),
      math: !!document.querySelector('#content .katex'),
      details: !!document.querySelector('#content details')
    })`)).result.value;
    if (Object.values(smoke).every(Boolean)) break;
    await delay(100);
  }
  assert.ok(Object.values(smoke).every(Boolean), JSON.stringify(smoke));
  console.log('PASS: sample.md headings, table, task list, highlighting, diagram, math, details');
} finally {
  ws?.close();
  app.kill();
}
