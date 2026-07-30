// QiuckPrompts companion — service worker: native messaging + tab orchestration.

const HOST = 'com.qiuckprompts.host';

let port = null;
let reconnectTimer = null;

function log(...args) {
  console.log('[qiuckprompts]', ...args);
}

function connectNative() {
  if (port) {
    try {
      port.disconnect();
    } catch { /* ignore */ }
    port = null;
  }
  try {
    port = chrome.runtime.connectNative(HOST);
  } catch (e) {
    log('connectNative threw', e);
    scheduleReconnect();
    return;
  }
  port.onMessage.addListener(onNativeMessage);
  port.onDisconnect.addListener(() => {
    const err = chrome.runtime.lastError;
    log('native disconnected', err && err.message);
    port = null;
    scheduleReconnect();
  });
  log('native connected');
}

function scheduleReconnect() {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(connectNative, 2000);
}

function reply(msg, extra) {
  if (!port) return;
  try {
    port.postMessage({ id: msg.id, ...extra });
  } catch (e) {
    log('postMessage failed', e);
  }
}

async function ensureTab(url) {
  const tabs = await chrome.tabs.query({});
  const want = new URL(url);
  for (const t of tabs) {
    if (!t.url) continue;
    try {
      const u = new URL(t.url);
      if (u.origin !== want.origin) continue;

      // Focus an existing tab on this origin.
      await chrome.tabs.update(t.id, { active: true });
      if (t.windowId != null) {
        try {
          await chrome.windows.update(t.windowId, { focused: true });
        } catch { /* ignore */ }
      }
      // Navigate only when path/query differ from the requested entry URL.
      if (u.pathname !== want.pathname || u.search !== want.search || u.hash !== want.hash) {
        await chrome.tabs.update(t.id, { url });
      }
      return t.id;
    } catch { /* ignore bad urls */ }
  }
  const created = await chrome.tabs.create({ url, active: true });
  return created.id;
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function sendToTab(tabId, message, attempts = 12) {
  let lastErr = 'no response';
  for (let i = 0; i < attempts; ++i) {
    try {
      const resp = await chrome.tabs.sendMessage(tabId, message);
      if (resp) return resp;
    } catch (e) {
      lastErr = String(e);
      // Content script may not be injected yet — try scripting.executeScript inject.
      if (i === 2 || i === 6) {
        try {
          await chrome.scripting.executeScript({
            target: { tabId, allFrames: true },
            files: ['content.js'],
          });
        } catch (inj) {
          lastErr = String(inj);
        }
      }
    }
    await sleep(250);
  }
  return { ok: false, error: lastErr };
}

async function onNativeMessage(msg) {
  log('native msg', msg && msg.cmd, msg && msg.id);
  if (!msg || typeof msg !== 'object') return;

  try {
    if (msg.cmd === 'ping') {
      reply(msg, { ok: true, cmd: 'pong', version: chrome.runtime.getManifest().version });
      return;
    }

    if (msg.cmd === 'prepareAndPaste') {
      const url = String(msg.url || '');
      const text = String(msg.text || '');
      const timeoutMs = Math.max(1000, Number(msg.timeoutMs) || 15000);
      if (!url) {
        reply(msg, { ok: false, error: 'missing url' });
        return;
      }
      const tabId = await ensureTab(url);
      // Give the SPA a moment after navigate/focus.
      await sleep(400);
      const resp = await sendToTab(tabId, {
        cmd: 'waitAndPaste',
        text,
        timeoutMs,
      });
      reply(msg, {
        ok: !!(resp && resp.ok),
        detail: (resp && (resp.detail || resp.error)) || '',
        error: resp && resp.ok ? undefined : (resp && resp.error) || 'paste failed',
        href: resp && resp.href,
      });
      return;
    }

    if (msg.cmd === 'prepare') {
      const url = String(msg.url || '');
      const timeoutMs = Math.max(1000, Number(msg.timeoutMs) || 15000);
      const tabId = await ensureTab(url);
      await sleep(400);
      const resp = await sendToTab(tabId, { cmd: 'findComposer', timeoutMs });
      reply(msg, {
        ok: !!(resp && resp.ok),
        detail: (resp && resp.detail) || '',
        error: resp && resp.ok ? undefined : (resp && resp.error) || 'composer not found',
      });
      return;
    }

    reply(msg, { ok: false, error: `unknown cmd ${msg.cmd}` });
  } catch (e) {
    reply(msg, { ok: false, error: String(e) });
  }
}

connectNative();

// Keep alive lightly when Chrome suspends SW — reconnect on startup events.
chrome.runtime.onStartup.addListener(connectNative);
chrome.runtime.onInstalled.addListener(connectNative);
