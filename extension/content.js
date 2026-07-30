// QiuckPrompts companion — content script: find chat composer + paste text.

const COMPOSER_SELECTORS = [
  '[contenteditable="true"]',
  'div[role="textbox"]',
  'textarea',
  '[data-lexical-editor="true"]',
  '.ql-editor',
  '[aria-label*="Message" i]',
  '[aria-label*="Ask" i]',
  '[aria-label*="prompt" i]',
  '[placeholder*="Ask" i]',
  '[placeholder*="Message" i]',
  '[placeholder*="Enter" i]',
];

function isVisible(el) {
  if (!el || !el.getBoundingClientRect) return false;
  const r = el.getBoundingClientRect();
  if (r.width < 40 || r.height < 18) return false;
  if (r.bottom < 0 || r.top > (window.innerHeight || 800) + 40) return false;
  const st = window.getComputedStyle(el);
  if (st.visibility === 'hidden' || st.display === 'none' || st.opacity === '0') return false;
  return true;
}

function scoreComposer(el) {
  let s = 0;
  const r = el.getBoundingClientRect();
  // Prefer lower half of viewport (chat boxes).
  if (r.top > (window.innerHeight || 800) * 0.35) s += 30;
  if (r.height >= 24 && r.height < 400) s += 10;
  if (r.width > 200) s += 10;
  const tag = (el.tagName || '').toLowerCase();
  if (tag === 'textarea') s += 15;
  if (el.getAttribute('contenteditable') === 'true') s += 20;
  if (el.getAttribute('role') === 'textbox') s += 20;
  const aria = (el.getAttribute('aria-label') || '') + ' ' + (el.getAttribute('placeholder') || '');
  if (/message|ask|prompt|chat|reply/i.test(aria)) s += 25;
  if (/search|address|omnibox|find/i.test(aria)) s -= 80;
  // Reject obvious top chrome.
  if (r.top < 80 && r.height < 50) s -= 40;
  return s;
}

function findComposer(root = document) {
  const candidates = [];
  for (const sel of COMPOSER_SELECTORS) {
    let list;
    try {
      list = root.querySelectorAll(sel);
    } catch {
      continue;
    }
    for (const el of list) {
      if (!isVisible(el)) continue;
      candidates.push({ el, score: scoreComposer(el) });
    }
  }
  candidates.sort((a, b) => b.score - a.score);
  if (candidates.length && candidates[0].score >= 20) return candidates[0].el;
  return null;
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/** Plain text → HTML that keeps line breaks in contenteditable. */
function plainToHtml(text) {
  return escapeHtml(text).replace(/\r\n|\n|\r/g, '<br>');
}

function fireInput(el, data) {
  try {
    el.dispatchEvent(
      new InputEvent('input', {
        bubbles: true,
        cancelable: true,
        inputType: 'insertFromPaste',
        data: data ?? null,
      }),
    );
  } catch {
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }
  try {
    el.dispatchEvent(new Event('change', { bubbles: true }));
  } catch {
    /* ignore */
  }
}

function setNativeValue(el, text) {
  const tag = (el.tagName || '').toLowerCase();
  if (tag !== 'textarea' && tag !== 'input' && !('value' in el)) return false;
  // Prefer real form controls only (contenteditable may also expose .value in some hosts).
  if (tag !== 'textarea' && tag !== 'input') return false;

  const proto =
    tag === 'textarea' ? window.HTMLTextAreaElement.prototype : window.HTMLInputElement.prototype;
  const desc = Object.getOwnPropertyDescriptor(proto, 'value');
  if (desc && desc.set) desc.set.call(el, text);
  else el.value = text;
  fireInput(el, text);
  return true;
}

function selectAllIn(el) {
  try {
    if (typeof el.select === 'function') {
      el.select();
      return;
    }
  } catch {
    /* ignore */
  }
  try {
    document.execCommand('selectAll', false, undefined);
  } catch {
    /* ignore */
  }
  try {
    const range = document.createRange();
    range.selectNodeContents(el);
    const sel = window.getSelection();
    if (sel) {
      sel.removeAllRanges();
      sel.addRange(range);
    }
  } catch {
    /* ignore */
  }
}

/**
 * ClipboardEvent paste — many React/Lexical/ProseMirror composers handle this
 * and preserve newlines from text/plain.
 */
function dispatchSyntheticPaste(el, text) {
  try {
    const dt = new DataTransfer();
    dt.setData('text/plain', text);
    dt.setData('text/html', plainToHtml(text));

    let evt;
    try {
      evt = new ClipboardEvent('paste', {
        bubbles: true,
        cancelable: true,
        clipboardData: dt,
      });
    } catch {
      evt = new Event('paste', { bubbles: true, cancelable: true });
      try {
        Object.defineProperty(evt, 'clipboardData', { value: dt });
      } catch {
        return false;
      }
    }

    // If the constructor dropped clipboardData, force it.
    if (!evt.clipboardData || typeof evt.clipboardData.getData !== 'function') {
      try {
        Object.defineProperty(evt, 'clipboardData', { value: dt });
      } catch {
        return false;
      }
    }

    el.dispatchEvent(evt);
    fireInput(el, text);
    return true;
  } catch {
    return false;
  }
}

/** Build DOM fragment with real <br> between lines (contenteditable-safe). */
function insertMultilineFragment(el, text) {
  try {
    selectAllIn(el);
    const sel = window.getSelection();
    if (!sel) return false;

    // Ensure a range inside el.
    if (!sel.rangeCount) {
      const r = document.createRange();
      r.selectNodeContents(el);
      r.collapse(true);
      sel.addRange(r);
    }

    sel.deleteFromDocument();
    const range = sel.getRangeAt(0);
    const lines = String(text).split(/\r\n|\n|\r/);
    const frag = document.createDocumentFragment();
    for (let i = 0; i < lines.length; ++i) {
      if (i > 0) frag.appendChild(document.createElement('br'));
      frag.appendChild(document.createTextNode(lines[i]));
    }
    const last = frag.lastChild;
    range.insertNode(frag);
    if (last) {
      range.setStartAfter(last);
      range.collapse(true);
      sel.removeAllRanges();
      sel.addRange(range);
    }
    fireInput(el, text);
    return true;
  } catch {
    return false;
  }
}

/**
 * Insert multi-line text. Prefer paths that keep `\n` as visible line breaks.
 * Never use bare textContent assignment as the primary path — HTML collapses
 * newlines in contenteditable unless white-space is pre-like.
 */
async function pasteInto(el, text) {
  const value = String(text ?? '');
  el.focus();
  selectAllIn(el);

  // 1) textarea / input — value keeps \n natively.
  if (setNativeValue(el, value)) return true;

  // 2) Synthetic paste (best for SPA chat boxes that listen for paste).
  if (dispatchSyntheticPaste(el, value)) {
    // If the framework swallowed paste without inserting, fall through.
    // We cannot always read back composer text (shadow/lexical). Assume ok when
    // default wasn't prevented in a way we can see — still try stronger paths if empty.
    // Cheap check: if element still looks empty and value was non-empty, continue.
    const sniff = (el.innerText || el.textContent || '').replace(/\s+/g, '');
    if (value.replace(/\s+/g, '').length === 0 || sniff.length > 0) return true;
  }

  // 3) insertText — Chrome often keeps \n as line breaks in contenteditable.
  try {
    selectAllIn(el);
    if (document.execCommand('insertText', false, value)) {
      fireInput(el, value);
      const sniff = (el.innerText || el.textContent || '').replace(/\s+/g, '');
      if (value.replace(/\s+/g, '').length === 0 || sniff.length > 0) return true;
    }
  } catch {
    /* ignore */
  }

  // 4) insertHTML with explicit <br> between lines.
  try {
    selectAllIn(el);
    if (document.execCommand('insertHTML', false, plainToHtml(value))) {
      fireInput(el, value);
      return true;
    }
  } catch {
    /* ignore */
  }

  // 5) DOM fragment with <br> nodes.
  if (insertMultilineFragment(el, value)) return true;

  // 6) Last resort — still better than one-line textContent: use innerHTML with br.
  try {
    el.innerHTML = plainToHtml(value);
    fireInput(el, value);
    return true;
  } catch {
    return false;
  }
}

function waitForComposer(timeoutMs) {
  const start = Date.now();
  return new Promise((resolve) => {
    const tick = () => {
      const el = findComposer();
      if (el) {
        resolve(el);
        return;
      }
      if (Date.now() - start >= timeoutMs) {
        resolve(null);
        return;
      }
      setTimeout(tick, 150);
    };
    tick();
  });
}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  (async () => {
    try {
      if (!msg || !msg.cmd) {
        sendResponse({ ok: false, error: 'no cmd' });
        return;
      }
      if (msg.cmd === 'ping') {
        sendResponse({ ok: true, detail: 'content pong', href: location.href });
        return;
      }
      if (msg.cmd === 'findComposer') {
        const el = findComposer();
        sendResponse({
          ok: !!el,
          detail: el ? `found ${el.tagName}` : 'no composer',
          href: location.href,
        });
        return;
      }
      if (msg.cmd === 'waitAndPaste') {
        const timeoutMs = Math.max(500, Number(msg.timeoutMs) || 15000);
        const el = await waitForComposer(timeoutMs);
        if (!el) {
          sendResponse({ ok: false, error: 'composer not found', href: location.href });
          return;
        }
        const text = String(msg.text ?? '');
        const nl = (text.match(/\r\n|\n|\r/g) || []).length;
        const pasted = await pasteInto(el, text);
        sendResponse({
          ok: pasted,
          detail: pasted
            ? `pasted ${text.length} chars newlines=${nl}`
            : 'paste failed',
          href: location.href,
        });
        return;
      }
      sendResponse({ ok: false, error: `unknown cmd ${msg.cmd}` });
    } catch (e) {
      sendResponse({ ok: false, error: String(e) });
    }
  })();
  return true; // async
});
