// Install only for development: npm install --prefix .build/qa playwright-core
// Start tools/preview.py first. This test never connects to a real lamp.
const { chromium } = require('../.build/qa/node_modules/playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Lamp = require('../web/core.js');

(async () => {
  const browser = await chromium.launch({ channel: 'msedge', headless: true });
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true, timezoneId: 'Europe/Madrid', reducedMotion: 'reduce' });
  const page = await context.newPage();
  const errors = [], external = [], writes = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (!request.url().startsWith('http://127.0.0.1:8765')) external.push(request.url());
    if (request.method() === 'POST') writes.push({ path: new URL(request.url()).pathname, body: request.postDataJSON() });
  });
  const click = id => page.locator('#' + id).click();
  const view = name => page.locator(`[data-view="${name}"]`).click();
  const draft = async () => Lamp.decode(await page.evaluate(() => JSON.parse(localStorage.getItem('lloona.studio.v2'))));
  const screenshot = name => page.screenshot({ path: path.resolve('.build/qa/screenshots/' + name + '.png'), fullPage: true });
  const waitText = (id, value) => page.waitForFunction(({ id, value }) => document.getElementById(id).textContent.includes(value), { id, value });
  try {
    fs.mkdirSync('.build/qa/screenshots', { recursive: true });
    await page.goto('http://127.0.0.1:8765');
    await waitText('connectionText', 'connectat');
    await screenshot('01-llum-mobile');
    await view('escenes');
    await page.locator('#savedScenes .scene-card:enabled').first().waitFor();
    await page.waitForFunction(() => document.querySelectorAll('#savedScenes .scene-card:disabled').length === 0);
    await screenshot('02-escenes-mobile');
    assert.equal(writes.length, 0, 'browsing never switches lamp mode');

    await page.locator('#inspirationScenes .scene-card').nth(1).click();
    await page.locator('#sceneDialog[open]').waitFor();
    await screenshot('03-scene-detail');
    await click('editPreset');
    await page.locator('#view-estudi:visible').waitFor();
    assert.equal(writes.length, 0, 'opening an editor never sends colours');
    assert.equal(await page.locator('#draftBadge').textContent(), 'esborrany', 'a built-in palette is not a saved scene');
    await screenshot('04-estudi-mobile');

    // Erasing used to fail on green/blue-only pixels. Verify all channels + W/F.
    await page.locator('.precision summary').click();
    await page.locator('#hexColor').fill('#000D00');
    await page.locator('#hexColor').press('Tab');
    await click('fillRing');
    assert.equal((await draft())[72].g, 13);
    await page.locator('[data-tool="erase"]').click();
    const editor = page.locator('#editorCanvas');
    await editor.focus();
    await editor.press(' ');
    assert.deepEqual((await draft())[72], { r: 0, g: 0, b: 0, w: 0, f: false });
    await click('undo');
    assert.equal((await draft())[72].g, 13);
    await click('redo');
    assert.equal((await draft())[72].g, 0);

    // Unsaved work must survive cancelling a scene replacement.
    const unsaved = await draft();
    await view('escenes');
    await page.locator('#inspirationScenes .scene-card').first().click();
    await click('editPreset');
    await page.locator('#confirmDialog[open]').waitFor();
    await click('cancelConfirm');
    assert.deepEqual(await draft(), unsaved);
    await click('closeScene');
    await view('estudi');

    // One fast stroke crossing the zero-degree seam, with pointer cancellation.
    await page.locator('[data-tool="paint"]').click();
    await editor.scrollIntoViewIfNeeded();
    const rect = await editor.boundingBox();
    const position = i => {
      const a = i / 72 * Math.PI * 2 - Math.PI / 2;
      return { clientX: rect.x + rect.width * (.5 + .325 * Math.cos(a)), clientY: rect.y + rect.height * (.5 + .325 * Math.sin(a)), pointerId: 7, pointerType: 'touch', bubbles: true };
    };
    // Real browser touch capture is exercised through Chromium's touch API.
    const client = await context.newCDPSession(page);
    const point = i => { const p = position(i); return { x: p.clientX, y: p.clientY, id: 7 }; };
    await client.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [point(70)] });
    await client.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [point(2)] });
    await client.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] });
    assert.equal((await draft())[72].g, 13, 'fast stroke fills seam without gaps');
    await click('undo');
    assert.equal((await draft())[72].g, 0, 'one undo reverses entire cancelled gesture');
    await click('redo');

    // RGB, independent W and the per-LED fire flag travel together in the picker.
    await page.locator('#whiteExact').fill('42');
    await page.locator('#whiteExact').press('Tab');
    await click('fireToggle');
    await click('fillRing');
    await page.locator('#hexColor').fill('#010203');
    await page.locator('#hexColor').press('Tab');
    await page.locator('#whiteExact').fill('0');
    await page.locator('#whiteExact').press('Tab');
    await click('fireToggle');
    await page.locator('[data-tool="picker"]').click();
    await editor.focus();
    await editor.press(' ');
    assert.equal(await page.locator('#hexColor').inputValue(), '#000D00');
    assert.equal(await page.locator('#whiteExact').inputValue(), '42');
    assert.equal(await page.locator('#fireToggle').getAttribute('aria-pressed'), 'true');

    // Save the local draft without applying or changing the hardware mode.
    const sceneName = 'L’òrbita "QA"';
    await page.locator('#presetName').fill(sceneName);
    const beforeSave = writes.length;
    await click('saveScene');
    if (await page.locator('#confirmDialog').isVisible()) await click('acceptConfirm');
    await waitText('toast', 'desada a la col·lecció');
    assert.equal(await page.locator('#draftBadge').textContent(), 'desat');
    assert.deepEqual(writes.slice(beforeSave).map(w => w.path), ['/api/presets/save']);
    await click('applyScene');
    await waitText('toast', 'aplicada al llum');
    assert.equal(writes.at(-1).path, '/api/led');
    assert.equal(writes.at(-1).body.ext.length, 648);
    assert.equal(writes.at(-1).body.int.length, 648);

    // A local draft and its channels survive reload; keyboard controls still work.
    const preserved = await draft();
    await page.reload();
    await waitText('connectionText', 'connectat');
    assert.deepEqual(await draft(), preserved);
    await view('escenes');
    const ownCard = page.getByRole('button', { name: `Obre l’escena ${sceneName}`, exact: true });
    await ownCard.waitFor();
    await page.waitForFunction(name => [...document.querySelectorAll('#savedScenes button')].some(b => b.getAttribute('aria-label') === 'Obre l’escena ' + name && !b.disabled), sceneName);
    await ownCard.click();
    await click('deletePreset');
    await page.locator('#confirmDialog[open]').waitFor();
    await click('cancelConfirm');
    assert.notEqual(writes.at(-1).path, '/api/presets/delete');
    await click('deletePreset');
    await page.locator('#confirmDialog[open]').waitFor();
    await click('acceptConfirm');
    await waitText('toast', 'eliminada');
    assert.equal(await page.locator('#draftBadge').textContent(), 'esborrany');

    await view('llum');
    await click('autoButton');
    await waitText('modeBadge', 'cicle natural');
    await page.locator('.time-travel summary').click();
    await page.locator('#simDate').fill('2026-12-24T21:30');
    await page.locator('#simulationForm button').click();
    await waitText('modeBadge', 'simulació');
    await click('autoButton');
    await waitText('modeBadge', 'cicle natural');

    // Failed saves must not claim success or clear the draft.
    await view('estudi');
    await page.locator('#presetName').fill('Error prova');
    await page.route('**/api/presets/save?*', route => route.fulfill({ status: 507, body: 'No hi ha espai per desar.' }));
    await click('saveScene');
    await waitText('toast', 'No hi ha espai');
    assert.deepEqual(await draft(), preserved);
    await page.unroute('**/api/presets/save?*');

    await page.route('**/api/status', route => route.abort());
    await click('connection');
    await waitText('connectionText', 'sense connexió');
    assert.equal(await page.locator('#applyScene').isDisabled(), true);
    assert.deepEqual(await draft(), preserved);
    await screenshot('05-offline-mobile');
    await page.unroute('**/api/status');
    await click('retryConnection');
    await waitText('connectionText', 'connectat');
    assert.equal(await page.locator('#applyScene').isEnabled(), true);

    for (const width of [320, 390, 430, 1024]) {
      await page.setViewportSize({ width, height: 844 });
      for (const name of ['llum', 'escenes', 'estudi']) {
        await view(name);
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
        assert.equal(overflow, false, `no horizontal overflow: ${name} at ${width}px`);
      }
      if (width === 320) await screenshot('06-estudi-small');
      if (width === 1024) { await view('llum'); await screenshot('07-llum-desktop'); }
    }
    assert.deepEqual(errors, [], 'no uncaught JavaScript errors');
    assert.deepEqual(external, [], 'UI loads without external resources');
    console.log('PASS: mobile layouts, scene previews, touch/keyboard painting, undo/redo, RGBW, local recovery, save/apply separation, delete confirmation, simulation, failed saves, disconnect/reconnect.');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
