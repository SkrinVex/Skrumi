// Browser layout and input regressions. The worker is a protocol stub: interpreter
// behaviour is covered separately by test_playground.js with the real WebAssembly.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

(async () => {
  const root = path.resolve(__dirname, '../../site');
  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, 'http://localhost');
      const file = path.resolve(root, '.' + decodeURIComponent(url.pathname), url.pathname.endsWith('/') ? 'index.html' : '');
      if (!file.startsWith(root + path.sep)) throw new Error('Outside site');
      res.setHeader('Content-Type', file.endsWith('.svg') ? 'image/svg+xml' : file.endsWith('.html') ? 'text/html; charset=utf-8' : 'text/plain; charset=utf-8');
      res.end(await fs.readFile(file));
    } catch { res.writeHead(404); res.end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  try {
    browser = await chromium.launch({
      ...(process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {}),
    });
    for (const mobile of [false, true]) {
      const context = await browser.newContext({
        viewport: { width: mobile ? 390 : 1280, height: 844 },
        isMobile: mobile, hasTouch: mobile,
      });
      const page = await context.newPage();
      await page.addInitScript(() => {
        window.inputs = [];
        window.runs = [];
        window.Worker = class {
          constructor() { setTimeout(() => this.emit({ type: 'ready', version: 'test' }), 0); }
          emit(data) { this.onmessage?.({ data }); }
          postMessage(message) {
            if (message.type === 'check') setTimeout(() => this.emit({ type: 'problems', id: message.id, problems: {} }), 0);
            if (message.type === 'run') window.runs.push(message);
            if (message.type === 'input') window.inputs.push(message);
            if (message.type === 'run' && message.canvas) setTimeout(() => this.emit({ type: 'window', width: 640, height: 500, title: 'Test' }), 0);
          }
          terminate() {}
        };
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/playground/`);
      const originalSource = await page.locator('#source').inputValue();
      await page.locator('#source').fill('/* значения */\nint n = 0xFF + 0b10 + 1_000; float f = 1.5e+3;');
      assert.deepEqual(await page.locator('#highlight .n').allTextContents(), ['0xFF', '0b10', '1_000', '1.5e+3']);
      assert.equal(await page.locator('#highlight .c').textContent(), '/* значения */');
      await page.locator('#examples').selectOption('expressions');
      await page.waitForFunction(() => document.querySelector('#source').value.includes('grapheme_length(family)'));
      assert((await page.locator('#highlight .n').allTextContents()).includes('0x22CC88'));
      await page.locator('#source').fill(originalSource);
      const savedProject = await context.newPage();
      await savedProject.addInitScript(() => {
        window.inputs = [];
        window.runs = [];
        window.Worker = class {
          constructor() { setTimeout(() => this.emit({ type: 'ready', version: 'test' }), 0); }
          emit(data) { this.onmessage?.({ data }); }
          postMessage(message) {
            if (message.type === 'check') setTimeout(() => this.emit({ type: 'problems', id: message.id, problems: {} }), 0);
            if (message.type === 'run') window.runs.push(message);
            if (message.type === 'input') window.inputs.push(message);
            if (message.type === 'run' && message.canvas) setTimeout(() => this.emit({ type: 'window', width: 640, height: 500, title: 'Test' }), 0);
          }
          terminate() {}
        };
      });
      await savedProject.addInitScript(() => {
        localStorage.setItem('skrumi-playground-files', JSON.stringify([
          {name: 'main.skm', text: 'include("helper.skm");'},
          {name: 'helper.skm', text: 'print(42);'}
        ]));
      });
      await savedProject.goto(`http://127.0.0.1:${server.address().port}/playground/`);
      await savedProject.waitForFunction(() => document.querySelector('#source').value.includes('helper.skm'));
      await savedProject.locator('#run').click();
      await savedProject.waitForFunction(() => window.runs.length > 0);
      const savedRun = await savedProject.evaluate(() => window.runs[0]);
      assert.equal(savedRun.entry, 'main.skm');
      assert(savedRun.files.some(file => file.name === 'helper.skm'));
      await savedProject.locator('#examples').selectOption('modules');
      await savedProject.waitForFunction(() => document.querySelector('#source').value.includes('using shapes'));
      await savedProject.locator('#run').click();
      await savedProject.waitForFunction(() => window.runs.length > 1);
      const exampleRun = await savedProject.evaluate(() => window.runs[1]);
      assert.equal(exampleRun.entry, 'main.skm');
      assert(exampleRun.files.every(file => file.name.endsWith('.skm')));
      assert(await savedProject.locator('.brand img').evaluate(img => img.complete && img.naturalWidth > 0));
      await savedProject.close();
      const source = page.locator('#source');
      await source.fill('print("' + 'long code '.repeat(60) + 'TAIL");\nprint("short");');
      await page.waitForTimeout(450); // Also exercise a checker-triggered render.
      async function checkTail() {
        const layout = await page.evaluate(() => {
          const editor = document.querySelector('#editor');
          const highlight = document.querySelector('#highlight');
          const source = document.querySelector('#source');
          editor.scrollLeft = editor.scrollWidth;
          const text = highlight.querySelector('.s').firstChild;
          const range = document.createRange();
          range.setStart(text, text.length - 5);
          range.setEnd(text, text.length - 1);
          const tail = range.getBoundingClientRect();
          const pre = highlight.getBoundingClientRect(), viewport = editor.getBoundingClientRect();
          return { scroll: editor.scrollLeft, tailLeft: tail.left, tailRight: tail.right,
            preRight: pre.right, viewportLeft: viewport.left, viewportRight: viewport.right,
            preWidth: highlight.clientWidth, textWidth: highlight.scrollWidth,
            sourceWidth: source.clientWidth, sourceScroll: source.scrollLeft };
        });
        assert(layout.scroll > 0, 'long lines must scroll');
        assert(layout.preRight >= layout.tailRight, 'highlight clips the tail of the long line');
        assert(layout.tailLeft >= layout.viewportLeft && layout.tailRight <= layout.viewportRight, 'tail must be visible after scrolling');
        assert(Math.abs(layout.preWidth - layout.sourceWidth) <= 1, 'input and highlight must have the same width');
        assert.equal(layout.sourceScroll, 0, 'textarea must not scroll independently');
      }
      await checkTail();
      await page.setViewportSize({ width: mobile ? 430 : 1000, height: 844 });
      await checkTail();
      await source.fill('print("short");');
      assert(await page.evaluate(() => {
        const e = document.querySelector('#editor');
        return e.scrollWidth <= e.clientWidth + 1;
      }), 'removing a long line must shrink the editor');
      await source.selectText();
      assert(await source.evaluate(s => s.selectionEnd > s.selectionStart), 'code must remain selectable');
      if (mobile) {
        await source.fill('using graphics;\nopen_window(160, 100, "Test");');
        await page.locator('#run').click();
        await page.locator('.pad').waitFor({ state: 'visible' });
        for (const selector of ['.pad', '.pad-key', '.type-button']) {
          assert.equal(await page.locator(selector).first().evaluate(e => getComputedStyle(e).userSelect), 'none');
          assert(await page.locator(selector).first().evaluate(e => !e.dispatchEvent(new Event('contextmenu', { bubbles: true, cancelable: true }))), 'context menu must be suppressed');
          assert(await page.locator(selector).first().evaluate(e => !e.dispatchEvent(new Event('selectstart', { bubbles: true, cancelable: true }))), 'selection must be suppressed');
        }
        // Large GUI windows must not clip the second row or keyboard on phones.
        for (const viewport of [{ width: 390, height: 720 }, { width: 320, height: 568 },
                                { width: 740, height: 360 }, { width: 430, height: 844 }]) {
          await page.setViewportSize(viewport);
          assert(await page.evaluate(() => {
            const pane = document.querySelector('#output-pane');
            pane.scrollTop = 0;
            return document.querySelector('.pad').getBoundingClientRect().bottom <= pane.getBoundingClientRect().bottom;
          }), `controls must fit without scrolling a hidden pane at ${viewport.width}x${viewport.height}`);
          for (const selector of ['.pad-key.up', '.pad-key.left', '.pad-key.down', '.pad-key.right', '.space', '.type-button']) {
            const button = page.locator(selector);
            await button.scrollIntoViewIfNeeded();
            assert(await button.evaluate(e => {
              const r = e.getBoundingClientRect();
              const hit = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
              const pane = document.querySelector('#output-pane').getBoundingClientRect();
              return e.contains(hit) && r.bottom <= pane.bottom && r.right <= pane.right;
            }), `${selector} must be visible and reachable at ${viewport.width}x${viewport.height}`);
          }
          await page.locator('.type-button').tap();
          assert(await page.locator('.type-input').evaluate(e => document.activeElement === e));
        }
        const key = page.locator('.pad-key.up');
        await key.dispatchEvent('pointerdown', { pointerId: 1, pointerType: 'touch' });
        await key.dispatchEvent('pointerdown', { pointerId: 2, pointerType: 'touch' });
        await page.waitForTimeout(600);
        assert(await key.evaluate(e => e.classList.contains('held')), 'long press must keep the key down');
        await key.dispatchEvent('pointerup', { pointerId: 1, pointerType: 'touch' });
        assert(await key.evaluate(e => e.classList.contains('held')), 'second finger still holds key');
        await key.dispatchEvent('pointercancel', { pointerId: 2, pointerType: 'touch' });
        assert(!(await key.evaluate(e => e.classList.contains('held'))), 'cancel must release key');
        assert.deepEqual(await page.evaluate(() => window.inputs.filter(e => e.kind === 'key').map(e => [e.code, e.down])), [[38, true], [38, false]]);
        await page.locator('.type-button').tap();
        assert(await page.locator('.type-input').evaluate(e => document.activeElement === e), 'keyboard button must still focus text input');
        await page.locator('#stop').click();
      }
      await context.close();
      console.log(`EDITOR_OK ${mobile ? 'mobile' : 'desktop'}`);
    }
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
