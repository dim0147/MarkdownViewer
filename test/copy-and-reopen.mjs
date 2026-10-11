// Run after build.bat: node test/copy-and-reopen.mjs
// Real WebView2 clipboard and native cross-process document-reuse regression test.
import { spawn, execFileSync } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';

const root = path.resolve(import.meta.dirname, '..');
const work = path.join(root, 'build', `copy-reopen-${Date.now()}`);
await mkdir(work, { recursive: true });
const first = path.join(work, 'first.md');
const second = path.join(work, 'second.md');
const source = 'Apply Phase 1 only. <tag> & "quotes" 中文 ✓\n    indented\n' + 'long line '.repeat(100) + '\n';
await writeFile(first, '# Copy test\n\n```text\n' + source + '```\n\n    indented block\n\n' + 'Paragraph.\n\n'.repeat(100));
await writeFile(second, '# Different file\n\n[First](first.md)\n');
const port = 19224;
const children = [];
function launch(file, profile = 'profile', debug = true) {
  const child = spawn(path.join(root, 'MarkdownViewer.exe'), [file], {
    cwd: root, windowsHide: false, // GUI exe: visible windows are needed for focus/minimize checks.
    env: { ...process.env, WEBVIEW2_USER_DATA_FOLDER: path.join(work, profile),
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: debug ? `--remote-debugging-port=${port}` : '' },
  });
  child.on('exit', code => { child.result = code; });
  children.push(child);
  return child;
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, label) {
  for (let i = 0; i < 100; i++) {
    if (await fn()) return;
    await delay(100);
  }
  assert.fail(label);
}
function ps(script) {
  return execFileSync('powershell.exe', ['-NoProfile', '-STA', '-Command',
    '[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); ' + script],
    { encoding: 'utf8', windowsHide: true }).trim();
}
const native = `Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ViewerTest {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
}
'@; `;
const clipboardBefore = ps('ConvertTo-Json -Compress -InputObject (Get-Clipboard -Raw)');
const app = launch(first);
let ws;
try {
  let target;
  await until(async () => {
    try {
      const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      target = pages.find(p => p.type === 'page' && p.url.includes('viewer.assets'));
      return !!target;
    } catch { return false; }
  }, 'WebView2 debug target');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let nextId = 0;
  const pending = new Map();
  ws.onmessage = event => {
    const data = JSON.parse(event.data);
    if (!pending.has(data.id)) return;
    const { resolve, reject } = pending.get(data.id);
    pending.delete(data.id);
    data.error ? reject(new Error(JSON.stringify(data.error))) : resolve(data.result);
  };
  function cdp(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++nextId;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });
  }
  async function evaluate(expression) {
    const result = await cdp('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  async function clickCopy(index = 0) {
    const point = await evaluate(`(() => {
      const button = document.querySelectorAll('.mv-code-copy')[${index}];
      button.scrollIntoView({block:'center'});
      const r = button.getBoundingClientRect();
      return {x:r.x+r.width/2,y:r.y+r.height/2};
    })()`);
    await cdp('Input.dispatchMouseEvent', { type: 'mousePressed', button: 'left', clickCount: 1, ...point });
    await cdp('Input.dispatchMouseEvent', { type: 'mouseReleased', button: 'left', clickCount: 1, ...point });
    await until(async () => await evaluate(`document.querySelectorAll('.mv-code-copy')[${index}].textContent !== 'Copy'`), 'copy feedback');
    return evaluate(`document.querySelectorAll('.mv-code-copy')[${index}].textContent`);
  }
  const clipboard = () => JSON.parse(ps('ConvertTo-Json -Compress -InputObject (Get-Clipboard -Raw)'))?.replace(/\r\n/g, '\n');
  await until(async () => await evaluate(`document.querySelectorAll('.mv-code-copy').length === 2`), 'fenced and indented copy controls');
  assert.equal(await clickCopy(), 'Copied!');
  assert.equal(clipboard(), source);
  console.log('PASS: copies full code text, whitespace, Unicode, and off-screen lines');
  await evaluate(`document.querySelector('pre').scrollLeft = 300`);
  assert.equal(await clickCopy(), 'Copied!');
  assert.equal(clipboard(), source);
  const shot = await cdp('Page.captureScreenshot', { format: 'png' });
  await writeFile(path.join(work, 'copy-light.png'), Buffer.from(shot.data, 'base64'));
  await evaluate(`window.chrome.webview.dispatchEvent(new MessageEvent('message',{
    data:{type:'config',config:JSON.stringify({theme:'dark'})}}))`);
  const darkShot = await cdp('Page.captureScreenshot', { format: 'png' });
  await writeFile(path.join(work, 'copy-dark.png'), Buffer.from(darkShot.data, 'base64'));
  console.log('PASS: Copy stays reachable while code scrolls horizontally (light/dark screenshots saved)');
  await evaluate(`window.realClipboard = navigator.clipboard.writeText.bind(navigator.clipboard);
    navigator.clipboard.writeText = () => Promise.reject(new Error('denied'));`);
  assert.equal(await clickCopy(1), 'Copied!');
  assert.equal(clipboard(), 'indented block\n');
  console.log('PASS: clipboard fallback copies indented code');
  await evaluate(`window.realExecCommand = document.execCommand.bind(document);
    document.execCommand = () => false;`);
  assert.equal(await clickCopy(), 'Copy failed');
  await evaluate(`navigator.clipboard.writeText = window.realClipboard; document.execCommand = window.realExecCommand;`);
  console.log('PASS: unavailable clipboard reports failure');

  await evaluate(`window.scrollTo(0,500); window.savedScroll = window.scrollY;
    window.originalHeading = document.querySelector('#content h1');`);
  ps(native + `$w = (Get-Process -Id ${app.pid}).MainWindowHandle; [ViewerTest]::ShowWindow($w,6) | Out-Null`);
  const duplicate = launch(path.join(work, '.', 'FIRST.md'));
  await until(() => duplicate.result === 0, 'duplicate launch exits successfully');
  assert.equal(ps(native + `$w = (Get-Process -Id ${app.pid}).MainWindowHandle;
    (($w -ne 0) -and ([ViewerTest]::GetForegroundWindow() -eq $w) -and -not [ViewerTest]::IsIconic($w))`), 'True');
  assert.equal(await evaluate(`window.originalHeading === document.querySelector('#content h1') && window.scrollY === window.savedScroll`), true);
  console.log('PASS: case/relative-path reopen restores and focuses minimized viewer without rerendering');

  await evaluate(`window.chrome.webview.dispatchEvent(new MessageEvent('message',{data:{type:'edit'}}));
    const editor = document.querySelector('#mv-ed-text');
    editor.value += '\\nUnsaved edit'; editor.dispatchEvent(new Event('input',{bubbles:true}));`);
  await until(async () => (await evaluate(`document.querySelector('#mv-ed-text').value`)).includes('Unsaved edit'), 'editor dirty');
  const dirtyDuplicate = launch(first);
  await until(() => dirtyDuplicate.result === 0, 'dirty duplicate exits');
  assert.ok((await evaluate(`document.querySelector('#mv-ed-text').value`)).includes('Unsaved edit'));
  console.log('PASS: reopening preserves unsaved edits without a discard prompt');
  await evaluate(`window.chrome.webview.postMessage('openfile:0')`);
  await delay(200);
  assert.ok((await evaluate(`document.querySelector('#mv-ed-text').value`)).includes('Unsaved edit'));
  console.log('PASS: in-app reopening the current file preserves the editor');
  const other = launch(second, 'other-profile', false);
  await delay(700);
  assert.equal(other.result, undefined, 'a different document gets its own viewer');
  // Launching another copy must reuse the second viewer too.
  const otherDuplicate = launch(second, 'unused-profile', false);
  await until(() => otherDuplicate.result === 0, 'second viewer also reused');
  console.log('PASS: different files keep separate viewers; each is reused independently');
  await evaluate(`window.chrome.webview.postMessage('openfile:1')`);
  await delay(200);
  assert.equal(ps(native + `$w = (Get-Process -Id ${other.pid}).MainWindowHandle;
    (($w -ne 0) -and ([ViewerTest]::GetForegroundWindow() -eq $w))`), 'True');
  assert.ok((await evaluate(`document.querySelector('#mv-ed-text').value`)).includes('Unsaved edit'));
  console.log('PASS: in-app opening an already-viewed file focuses the other viewer and preserves current edits');

  const raceFile = path.join(work, 'race.md');
  await writeFile(raceFile, '# Concurrent launch');
  const racers = [launch(raceFile, 'race-profile', false), launch(raceFile, 'race-profile', false)];
  await until(() => racers.filter(p => p.result === 0).length === 1, 'one simultaneous launch exits');
  assert.equal(racers.filter(p => p.result === undefined).length, 1);
  console.log('PASS: simultaneous launches claim a single viewer before WebView2 readiness');
} finally {
  ws?.close();
  for (const child of children) if (child.result === undefined) child.kill();
  // Restore text clipboard contents after exercising real copy actions.
  if (clipboardBefore && clipboardBefore !== 'null') {
    const text = JSON.parse(clipboardBefore);
    const encoded = Buffer.from(text || '', 'utf8').toString('base64');
    ps(`Set-Clipboard -Value ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('${encoded}')))`);
  }
}
