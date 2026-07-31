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
    } catch {
      /* ignore */
    }
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

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

/** Wait until tab reports status=complete (SPA still may hydrate after). */
function waitTabComplete(tabId, timeoutMs = 25000) {
  return new Promise((resolve) => {
    let settled = false;
    let timer = null;
    const finish = (ok) => {
      if (settled) return;
      settled = true;
      if (timer != null) {
        clearTimeout(timer);
        timer = null;
      }
      try {
        chrome.tabs.onUpdated.removeListener(onUpdated);
      } catch {
        /* ignore */
      }
      resolve(ok);
    };
    const onUpdated = (id, info) => {
      if (id === tabId && info.status === 'complete') finish(true);
    };
    chrome.tabs.onUpdated.addListener(onUpdated);
    chrome.tabs
      .get(tabId)
      .then((t) => {
        if (t && t.status === 'complete') finish(true);
      })
      .catch(() => {
        /* keep waiting */
      });
    timer = setTimeout(() => finish(false), timeoutMs);
  });
}

/**
 * Focus or open a tab for url.
 * Returns { tabId, cold } where cold=true if we created a tab or navigated.
 */
async function ensureTab(url) {
  const tabs = await chrome.tabs.query({});
  const want = new URL(url);
  for (const t of tabs) {
    if (!t.url) continue;
    try {
      const u = new URL(t.url);
      if (u.origin !== want.origin) continue;

      await chrome.tabs.update(t.id, { active: true });
      if (t.windowId != null) {
        try {
          await chrome.windows.update(t.windowId, { focused: true });
        } catch {
          /* ignore */
        }
      }
      let cold = false;
      if (u.pathname !== want.pathname || u.search !== want.search || u.hash !== want.hash) {
        await chrome.tabs.update(t.id, { url });
        cold = true;
        await waitTabComplete(t.id);
      }
      return { tabId: t.id, cold };
    } catch {
      /* ignore bad urls */
    }
  }
  const created = await chrome.tabs.create({ url, active: true });
  await waitTabComplete(created.id);
  return { tabId: created.id, cold: true };
}

async function sendToTab(tabId, message, attempts = 16) {
  let lastErr = 'no response';
  for (let i = 0; i < attempts; ++i) {
    try {
      const resp = await chrome.tabs.sendMessage(tabId, message);
      if (resp) return resp;
    } catch (e) {
      lastErr = String(e);
      // Content script may not be injected yet (especially right after navigation).
      if (i === 1 || i === 4 || i === 8) {
        try {
          await chrome.scripting.executeScript({
            target: { tabId, allFrames: false },
            files: ['content.js'],
          });
        } catch (inj) {
          lastErr = String(inj);
        }
      }
    }
    await sleep(300);
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
      const { tabId, cold } = await ensureTab(url);
      // Cold open / navigate: SPA shell is not ready at status=complete.
      if (cold) {
        await sleep(900);
      } else {
        await sleep(250);
      }
      const resp = await sendToTab(tabId, {
        cmd: 'waitAndPaste',
        text,
        timeoutMs,
        cold: !!cold,
      });
      reply(msg, {
        ok: !!(resp && resp.ok),
        detail: (resp && (resp.detail || resp.error)) || '',
        error: resp && resp.ok ? undefined : (resp && resp.error) || 'paste failed',
        href: resp && resp.href,
        cold: !!cold,
      });
      return;
    }

    if (msg.cmd === 'prepare') {
      const url = String(msg.url || '');
      const timeoutMs = Math.max(1000, Number(msg.timeoutMs) || 15000);
      const { tabId, cold } = await ensureTab(url);
      if (cold) await sleep(900);
      else await sleep(250);
      const resp = await sendToTab(tabId, { cmd: 'findComposer', timeoutMs, cold: !!cold });
      reply(msg, {
        ok: !!(resp && resp.ok),
        detail: (resp && resp.detail) || '',
        error: resp && resp.ok ? undefined : (resp && resp.error) || 'composer not found',
        cold: !!cold,
      });
      return;
    }

    reply(msg, { ok: false, error: `unknown cmd ${msg.cmd}` });
  } catch (e) {
    reply(msg, { ok: false, error: String(e) });
  }
}

connectNative();

chrome.runtime.onStartup.addListener(connectNative);
chrome.runtime.onInstalled.addListener(connectNative);
