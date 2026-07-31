// QiuckPrompts companion — content script: find chat composer + paste text.
// Guard: chrome.scripting.executeScript may inject this file again; do not stack listeners.
if (globalThis.__qiuckpromptsContentLoaded) {
  // Already armed in this frame.
} else {
  globalThis.__qiuckpromptsContentLoaded = true;
  boot();
}

function boot() {
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
    if (r.top > (window.innerHeight || 800) * 0.35) s += 30;
    if (r.height >= 24 && r.height < 400) s += 10;
    if (r.width > 200) s += 10;
    const tag = (el.tagName || '').toLowerCase();
    if (tag === 'textarea') s += 15;
    if (el.getAttribute('contenteditable') === 'true') s += 20;
    if (el.getAttribute('role') === 'textbox') s += 20;
    const aria =
      (el.getAttribute('aria-label') || '') + ' ' + (el.getAttribute('placeholder') || '');
    if (/message|ask|prompt|chat|reply/i.test(aria)) s += 25;
    if (/search|address|omnibox|find/i.test(aria)) s -= 80;
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

  function sleep(ms) {
    return new Promise((r) => setTimeout(r, ms));
  }

  /** Empty the composer so a single insert cannot stack on prior text. */
  function clearComposer(el) {
    el.focus();
    const tag = (el.tagName || '').toLowerCase();
    if (tag === 'textarea' || tag === 'input') {
      setNativeValue(el, '');
      return;
    }
    selectAllIn(el);
    try {
      document.execCommand('delete', false, undefined);
    } catch {
      /* ignore */
    }
    try {
      // Lexical/ProseMirror often need insertText('') after selectAll to clear.
      document.execCommand('insertText', false, '');
    } catch {
      /* ignore */
    }
    try {
      el.innerHTML = '';
    } catch {
      /* ignore */
    }
    try {
      while (el.firstChild) el.removeChild(el.firstChild);
    } catch {
      /* ignore */
    }
    fireInput(el, '');
  }

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
      if (!evt.clipboardData || typeof evt.clipboardData.getData !== 'function') {
        try {
          Object.defineProperty(evt, 'clipboardData', { value: dt });
        } catch {
          return false;
        }
      }
      // Do NOT fireInput after paste — Lexical paste handlers already update the model.
      // A second input event can cause double-insert in some hosts.
      el.dispatchEvent(evt);
      return true;
    } catch {
      return false;
    }
  }

  function insertMultilineFragment(el, text) {
    try {
      selectAllIn(el);
      const sel = window.getSelection();
      if (!sel) return false;
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

  function sniffText(el) {
    try {
      if ('value' in el && typeof el.value === 'string' && el.value.length) return el.value;
    } catch {
      /* ignore */
    }
    return el.innerText || el.textContent || '';
  }

  /**
   * Insert multi-line text exactly once.
   * Important: Lexical/React often apply paste asynchronously and leave innerText
   * empty for a tick — do NOT fall through to other insert methods or we stack copies.
   */
  async function pasteInto(el, text) {
    const value = String(text ?? '');
    el.focus();

    // 1) textarea / input — single value assignment.
    if (setNativeValue(el, value)) return true;

    // 2) Clear, then ONE contenteditable strategy.
    clearComposer(el);
    await sleep(0);

    // Prefer insertText: replaces selection, keeps \n, widely supported in Chrome CE.
    try {
      selectAllIn(el);
      if (document.queryCommandSupported && !document.queryCommandSupported('insertText')) {
        /* fall through */
      } else if (document.execCommand('insertText', false, value)) {
        fireInput(el, value);
        return true;
      }
    } catch {
      /* ignore */
    }

    // 3) Synthetic paste only (no further methods after this succeeds at dispatch).
    clearComposer(el);
    await sleep(0);
    if (dispatchSyntheticPaste(el, value)) {
      // Give Lexical a frame to apply; do not chain more inserts.
      await sleep(30);
      return true;
    }

    // 4) insertHTML with <br>
    clearComposer(el);
    try {
      selectAllIn(el);
      if (document.execCommand('insertHTML', false, plainToHtml(value))) {
        fireInput(el, value);
        return true;
      }
    } catch {
      /* ignore */
    }

    // 5) DOM fragment
    clearComposer(el);
    if (insertMultilineFragment(el, value)) return true;

    // 6) last resort
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
          const after = sniffText(el);
          // Detect accidental multi-insert (payload appears more than once).
          const compact = (s) => String(s).replace(/\s+/g, '');
          const cPay = compact(text);
          const cAfter = compact(after);
          let detail = pasted
            ? `pasted ${text.length} chars newlines=${nl}`
            : 'paste failed';
          if (pasted && cPay.length > 20 && cAfter.includes(cPay + cPay)) {
            detail += ' WARN:duplicate_detected';
            // Best-effort one reset to a single copy.
            clearComposer(el);
            await sleep(0);
            try {
              selectAllIn(el);
              document.execCommand('insertText', false, text);
              fireInput(el, text);
              detail += '+repaired';
            } catch {
              /* ignore */
            }
          }
          sendResponse({
            ok: pasted,
            detail,
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
}
