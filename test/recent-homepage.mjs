// Run after build.bat: node test/recent-homepage.mjs
// Exercises the real homepage/host protocol; restores the user's MRU afterward.
import { spawn, execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile, rm } from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';

const root = path.resolve(import.meta.dirname, '..');
const work = path.join(root, 'build', `recent-homepage-${Date.now()}`);
const store = path.join(process.env.APPDATA, 'MarkdownViewer', 'recent.txt');
await mkdir(path.dirname(store), { recursive: true });
let before;
try { before = await readFile(store); } catch (e) { if (e.code !== 'ENOENT') throw e; }
const first = path.join(work, 'first', "Notes & ' 中文.md");
const second = path.join(work, 'second', path.basename(first));
for (const [file, heading] of [[first, 'First document'], [second, 'Second document']]) {
  await mkdir(path.dirname(file), { recursive: true });
  await writeFile(file, `# ${heading}\n`);
}
const port = 19225;
const children = [];
const sockets = [];
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, label) {
  for (let i = 0; i < 100; i++) {
    if (await fn()) return;
    await delay(100);
  }
  assert.fail(label);
}
async function launch(profile) {
  const app = spawn(path.join(root, 'MarkdownViewer.exe'), [], {
    cwd: root, windowsHide: false, // Visible GUI required for screenshots and keyboard activation.
    env: { ...process.env, WEBVIEW2_USER_DATA_FOLDER: path.join(work, profile),
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port}` },
  });
  children.push(app);
  let target;
  await until(async () => {
    try {
      target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json())
        .find(p => p.type === 'page' && p.url.includes('viewer.assets'));
      return !!target;
    } catch { return false; }
  }, 'WebView2 target');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  sockets.push(ws);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 0;
  const pending = new Map();
  ws.onmessage = event => {
    const data = JSON.parse(event.data);
    const request = pending.get(data.id);
    if (!request) return;
    pending.delete(data.id);
    data.error ? request.reject(new Error(JSON.stringify(data.error))) : request.resolve(data.result);
  };
  const cdp = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++nextId;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`Timed out: ${method}`));
    }, 10000);
    pending.set(id, {
      resolve: result => { clearTimeout(timer); resolve(result); },
      reject: error => { clearTimeout(timer); reject(error); },
    });
    ws.send(JSON.stringify({ id, method, params }));
  });
  async function evaluate(expression) {
    const result = await cdp('Runtime.evaluate', { expression, returnByValue: true });
    assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  async function screenshot(name) {
    const shot = await cdp('Page.captureScreenshot', { format: 'png' });
    await writeFile(path.join(work, name), Buffer.from(shot.data, 'base64'));
  }
  return { app, cdp, evaluate, screenshot };
}
const dispatch = data => `window.chrome.webview.dispatchEvent(new MessageEvent('message',{data:${JSON.stringify(data)}}))`;
try {
  await writeFile(store, '');
  const { app, cdp, evaluate, screenshot } = await launch('profile');
  await until(async () => await evaluate(`!!document.querySelector('#mv-recent-empty')`), 'homepage ready');
  assert.equal(await evaluate(`document.querySelector('#mv-recent-empty').hidden`), false);
  assert.equal(await evaluate(`document.querySelectorAll('.mv-recent-file').length`), 0);
  console.log('PASS: empty homepage shows a helpful empty state');

  await writeFile(store, [first, second].join('\r\n') + '\r\n');
  await evaluate(`window.chrome.webview.postMessage('ready')`);
  await until(async () => await evaluate(`document.querySelectorAll('.mv-recent-file').length === 2`), 'persisted recent files');
  assert.deepEqual(await evaluate(`Array.from(document.querySelectorAll('.mv-recent-path'), n => n.textContent)`), [first, second]);
  assert.deepEqual(await evaluate(`Array.from(document.querySelectorAll('.mv-recent-name'), n => n.textContent)`), [path.basename(first), path.basename(second)]);
  await screenshot('homepage-light.png');
  await evaluate(dispatch({ type: 'config', config: JSON.stringify({ theme: 'dark' }) }));
  await screenshot('homepage-dark.png');
  await cdp('Emulation.setDeviceMetricsOverride', { width: 480, height: 600, deviceScaleFactor: 1, mobile: false });
  assert.equal(await evaluate(`document.documentElement.scrollWidth <= window.innerWidth`), true);
  await screenshot('homepage-narrow.png');
  console.log('PASS: persisted order, duplicate names, Unicode/quotes, light/dark and narrow layouts');

  execFileSync('powershell.exe', ['-NoProfile', '-Command', `
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RecentMenuTest {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
}
'@;
    $viewerHandle = (Get-Process -Id ${app.pid}).MainWindowHandle;
    [RecentMenuTest]::PostMessage($viewerHandle, 0x111, [IntPtr]700, [IntPtr]::Zero) | Out-Null
  `], { windowsHide: true });
  await until(async () => await evaluate(`document.querySelectorAll('.mv-recent-file').length === 0 && !document.querySelector('#mv-recent-empty').hidden`), 'clear menu updates homepage');
  await writeFile(store, [first, second].join('\r\n') + '\r\n');
  await evaluate(`window.chrome.webview.postMessage('ready')`);
  await until(async () => await evaluate(`document.querySelectorAll('.mv-recent-file').length === 2`), 'restore recent fixtures');
  console.log('PASS: Clear recent files immediately updates the homepage');

  for (const index of ['-1', '99', 'garbage', '0junk'])
    await evaluate(`window.chrome.webview.postMessage(${JSON.stringify('openrecent:' + index)})`);
  await delay(200);
  assert.equal(await evaluate(`!!document.querySelector('.welcome')`), true);
  console.log('PASS: invalid recent indexes do not open a document');

  // A different viewer can reorder recent.txt after the homepage was displayed.
  await writeFile(store, [second, first].join('\r\n') + '\r\n');
  await evaluate(`document.querySelector('.mv-recent-file').focus()`);
  await cdp('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', text: '\r', windowsVirtualKeyCode: 13 });
  await cdp('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
  await until(async () => await evaluate(`document.querySelector('#content h1')?.textContent === 'First document'`), 'keyboard open');
  assert.equal((await readFile(store, 'utf8')).split(/\r?\n/)[0], first);
  console.log('PASS: keyboard opens the displayed file despite MRU reordering, and promotes it');

  // Return the host to its homepage by restarting the test viewer.
  const exited = new Promise(resolve => app.once('exit', resolve));
  app.kill();
  await exited;
  await delay(500);
  const next = await launch('next-profile');
  await until(async () => await next.evaluate(`document.querySelectorAll('.mv-recent-file').length === 2`), 'restart restores list');
  await next.evaluate(`document.querySelectorAll('.mv-recent-file')[1].click()`);
  await until(async () => await next.evaluate(`document.querySelector('#content h1')?.textContent === 'Second document'`), 'mouse open');
  console.log('PASS: clicking a recent file renders that document after restart');
} finally {
  for (const ws of sockets) ws.close();
  for (const app of children) {
    if (app.exitCode !== null || app.signalCode !== null) continue;
    const exited = new Promise(resolve => app.once('exit', resolve));
    app.kill();
    await exited;
  }
  if (before === undefined) await rm(store, { force: true });
  else await writeFile(store, before);
}
