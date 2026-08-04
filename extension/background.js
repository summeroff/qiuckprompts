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
 * True when the existing tab is already on the target AI app.
 * Do NOT re-navigate merely because query/hash/conversation id differ — that
 * forced cold=1 on every Meta/Gemini hotkey and re-hydrated the composer.
 *
 * Rules:
 * - origin must match
 * - want path "/" (meta.ai/, chatgpt.com/): any path on that origin is fine
 * - otherwise accept exact path or prefix (gemini /app → /app/xyz)
 */
function isAlreadyOnApp(u, want) {
  if (u.origin !== want.origin) return false;
  let wantPath = want.pathname || '/';
  if (wantPath.length > 1 && wantPath.endsWith('/')) wantPath = wantPath.slice(0, -1);
  if (wantPath === '/' || wantPath === '') return true;
  const path = u.pathname || '/';
  if (path === wantPath) return true;
  const prefix = wantPath.endsWith('/') ? wantPath : wantPath + '/';
  return path.startsWith(prefix);
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
      if (!isAlreadyOnApp(u, want)) {
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

async function sendToTab(tabId, message, attempts = 16, deadlineMs = 0) {
  let lastErr = 'no response';
  for (let i = 0; i < attempts; ++i) {
    if (deadlineMs && Date.now() >= deadlineMs) {
      return { ok: false, error: 'timeout waiting for content script' };
    }
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
    const slice = deadlineMs ? Math.min(300, Math.max(50, deadlineMs - Date.now())) : 300;
    if (slice <= 0) break;
    await sleep(slice);
  }
  return { ok: false, error: lastErr };
}

async function tabStillActiveOnOrigin(tabId, wantOrigin, cancelOnFocusSwitch = true) {
  try {
    const t = await chrome.tabs.get(tabId);
    if (!t) return { ok: false, error: 'tab gone' };
    if (wantOrigin && t.url) {
      try {
        if (new URL(t.url).origin !== wantOrigin) {
          return { ok: false, error: 'tab navigated off AI origin' };
        }
      } catch {
        /* ignore bad url */
      }
    }
    if (!cancelOnFocusSwitch) return { ok: true };

    // Tab.active only means selected in its window — still true if another app has OS focus.
    if (t.active === false) return { ok: false, error: 'tab inactive / focus switched' };
    if (t.windowId != null) {
      try {
        const w = await chrome.windows.get(t.windowId);
        if (w && w.focused === false) {
          return { ok: false, error: 'browser window not focused' };
        }
      } catch {
        /* ignore — some hosts deny windows.get */
      }
    }
    return { ok: true };
  } catch (e) {
    return { ok: false, error: String(e) };
  }
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
      // Hard cap 10s for form wait — matches product rule: cancel rather than surprise-paste.
      const timeoutMs = Math.min(10000, Math.max(1000, Number(msg.timeoutMs) || 10000));
      // Default true; tray can pass cancelOnFocusSwitch:false to disable focus leave cancel.
      const cancelOnFocusSwitch = msg.cancelOnFocusSwitch !== false;
      if (!url) {
        reply(msg, { ok: false, error: 'missing url' });
        return;
      }
      let wantOrigin = '';
      try {
        wantOrigin = new URL(url).origin;
      } catch {
        /* ignore */
      }
      const deadline = Date.now() + timeoutMs + 1500;
      const { tabId, cold } = await ensureTab(url);
      // Cold open / navigate: SPA shell is not ready at status=complete.
      if (cold) {
        await sleep(900);
      } else {
        await sleep(250);
      }
      const focusCheck = await tabStillActiveOnOrigin(tabId, wantOrigin, cancelOnFocusSwitch);
      if (!focusCheck.ok) {
        reply(msg, {
          ok: false,
          error: focusCheck.error || 'tab inactive / focus switched',
          cold: !!cold,
        });
        return;
      }
      const remain = Math.max(500, deadline - Date.now());
      const resp = await sendToTab(
        tabId,
        {
          cmd: 'waitAndPaste',
          text,
          timeoutMs: Math.min(timeoutMs, remain),
          cold: !!cold,
          wantOrigin,
          cancelOnFocusSwitch,
        },
        16,
        deadline,
      );
      // Final guard: if user left while content script worked, still report cancel.
      const after = await tabStillActiveOnOrigin(tabId, wantOrigin, cancelOnFocusSwitch);
      if (resp && resp.ok && !after.ok) {
        reply(msg, {
          ok: false,
          error: after.error || 'tab inactive after paste',
          detail: resp.detail,
          cold: !!cold,
        });
        return;
      }
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
      const timeoutMs = Math.min(10000, Math.max(1000, Number(msg.timeoutMs) || 10000));
      const cancelOnFocusSwitch = msg.cancelOnFocusSwitch !== false;
      let wantOrigin = '';
      try {
        wantOrigin = new URL(url).origin;
      } catch {
        /* ignore */
      }
      const deadline = Date.now() + timeoutMs + 1500;
      const { tabId, cold } = await ensureTab(url);
      if (cold) await sleep(900);
      else await sleep(250);
      const focusCheck = await tabStillActiveOnOrigin(tabId, wantOrigin, cancelOnFocusSwitch);
      if (!focusCheck.ok) {
        reply(msg, { ok: false, error: focusCheck.error || 'tab inactive', cold: !!cold });
        return;
      }
      const remain = Math.max(500, deadline - Date.now());
      const resp = await sendToTab(
        tabId,
        {
          cmd: 'findComposer',
          timeoutMs: Math.min(timeoutMs, remain),
          cold: !!cold,
          wantOrigin,
          cancelOnFocusSwitch,
        },
        16,
        deadline,
      );
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
