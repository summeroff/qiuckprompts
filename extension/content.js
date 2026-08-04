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
    const vh = window.innerHeight || 800;
    // Prefer lower half / bottom dock (chat composers).
    if (r.top > vh * 0.35) s += 30;
    if (r.top > vh * 0.55) s += 15;
    if (r.height >= 24 && r.height < 400) s += 10;
    if (r.height >= 40) s += 8;
    if (r.width > 200) s += 10;
    if (r.width > 360) s += 8;
    const tag = (el.tagName || '').toLowerCase();
    if (tag === 'textarea') s += 15;
    if (el.getAttribute('contenteditable') === 'true') s += 20;
    if (el.getAttribute('role') === 'textbox') s += 20;
    if (el.getAttribute('data-lexical-editor') === 'true') s += 25;
    const aria =
      (el.getAttribute('aria-label') || '') +
      ' ' +
      (el.getAttribute('placeholder') || '') +
      ' ' +
      (el.getAttribute('aria-placeholder') || '');
    if (/message|ask|prompt|chat|reply|gemini|enter a prompt/i.test(aria)) s += 30;
    if (/search|address|omnibox|find|filter/i.test(aria)) s -= 80;
    // Tiny top chrome / transient loaders.
    if (r.top < 80 && r.height < 50) s -= 40;
    if (r.height < 28 && r.width < 280) s -= 25;
    // Reject non-editable shells.
    if (el.getAttribute('contenteditable') === 'false') s -= 50;
    if (el.getAttribute('aria-disabled') === 'true' || el.disabled) s -= 50;
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
    // Cold Gemini shells sometimes expose weak textboxes early — require a real score.
    if (candidates.length && candidates[0].score >= 35) return candidates[0].el;
    return null;
  }

  function composerKey(el) {
    if (!el) return '';
    try {
      const r = el.getBoundingClientRect();
      return [
        el.tagName,
        el.getAttribute('role') || '',
        el.getAttribute('aria-label') || '',
        el.getAttribute('data-lexical-editor') || '',
        Math.round(r.top),
        Math.round(r.left),
        Math.round(r.width),
        Math.round(r.height),
      ].join('|');
    } catch {
      return String(el);
    }
  }

  /**
   * Wait until the same high-scoring composer is observed N times in a row.
   * Avoids pasting into Gemini/Meta load shells that are replaced on hydrate.
   * Resolves { el, cancelReason }. cancelReason set if tab hidden / left / timed out empty.
   */
  function waitForStableComposer(timeoutMs, opts = {}) {
    const cold = !!opts.cold;
    const needStable = cold ? 3 : 2;
    const pollMs = cold ? 200 : 150;
    const start = Date.now();
    let lastKey = '';
    let streak = 0;
    let lastEl = null;

    return new Promise((resolve) => {
      const tick = () => {
        const abort = pasteAbortReason(opts);
        if (abort) {
          resolve({ el: null, cancelReason: abort });
          return;
        }
        const el = findComposer();
        if (el) {
          const key = composerKey(el);
          // Always refresh lastEl so we never return a detached node if SPA
          // replaced the element with one that shares the same key.
          lastEl = el;
          if (key && key === lastKey) {
            streak += 1;
          } else {
            lastKey = key;
            streak = 1;
          }
          if (streak >= needStable) {
            resolve({ el: lastEl, cancelReason: null });
            return;
          }
        } else {
          lastKey = '';
          streak = 0;
          lastEl = null;
        }
        if (Date.now() - start >= timeoutMs) {
          // Never hand back an unstable lastEl on timeout — that defeats stability + cancel.
          resolve({
            el: null,
            cancelReason: 'composer not found (timeout)',
          });
          return;
        }
        setTimeout(tick, pollMs);
      };
      tick();
    });
  }

  /** Abort paste if the user left the tab/window or navigated off the AI origin. */
  function pasteAbortReason(opts = {}) {
    // When cancelOnFocusSwitch is explicitly false, skip hide/focus abort (timeout still applies).
    const cancelFocus = opts.cancelOnFocusSwitch !== false;
    if (cancelFocus) {
      try {
        if (document.hidden || document.visibilityState === 'hidden') {
          return 'tab hidden / not focused';
        }
      } catch {
        /* ignore */
      }
    }
    const wantOrigin = opts.wantOrigin ? String(opts.wantOrigin) : '';
    if (wantOrigin) {
      try {
        if (location.origin !== wantOrigin) {
          return 'navigated off AI origin (' + location.origin + ')';
        }
      } catch {
        /* ignore */
      }
    }
    return null;
  }

  function textStillPresent(el, text) {
    const pay = compactText(text);
    if (!pay) return true;
    const got = compactText(sniffText(el));
    if (!got) return false;
    // Require a meaningful prefix match (full paste may normalize quotes/newlines).
    const probe = pay.slice(0, Math.min(48, pay.length));
    return got.includes(probe);
  }

  async function pasteWithVerify(el, text) {
    const ok = await pasteInto(el, text);
    if (!ok) return { ok: false, detail: 'paste failed' };
    // SPA hydrate can wipe a successful DOM write a moment later.
    // Lexical may also apply a delayed second insert — wait long enough to see it.
    await sleep(280);
    if (!textStillPresent(el, text)) {
      return { ok: false, detail: 'paste wiped after hydrate' };
    }
    if (looksDuplicated(el, text)) {
      return { ok: true, detail: 'paste verified', duplicated: true };
    }
    return { ok: true, detail: 'paste verified', duplicated: false };
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

  /**
   * Notify React/controlled hosts of a DOM change.
   * NEVER use inputType 'insertFromPaste' here after execCommand('insertText') —
   * Lexical (Meta AI) treats that as a second paste and stacks a collapsed copy.
   */
  function fireInput(el, data, inputType = 'insertText') {
    try {
      el.dispatchEvent(
        new InputEvent('input', {
          bubbles: true,
          cancelable: true,
          inputType: inputType || 'insertText',
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

  function compactText(s) {
    return String(s || '').replace(/\s+/g, '');
  }

  /** How many times `needle` appears non-overlapping in `hay`. */
  function countOccurrences(hay, needle) {
    if (!needle || needle.length < 8) return 0;
    let n = 0;
    let idx = 0;
    while ((idx = hay.indexOf(needle, idx)) !== -1) {
      n += 1;
      idx += needle.length;
    }
    return n;
  }

  function looksDuplicated(el, text) {
    const pay = compactText(text);
    if (pay.length < 20) return false;
    const got = compactText(sniffText(el));
    if (!got) return false;
    if (got.includes(pay + pay)) return true;
    if (countOccurrences(got, pay) >= 2) return true;
    // Collapsed + full copies: length ~2x with payload prefix present twice-ish.
    if (got.length >= Math.floor(pay.length * 1.6) && countOccurrences(got, pay.slice(0, 48)) >= 2) {
      return true;
    }
    return false;
  }

  function looksSingleGood(el, text) {
    const pay = compactText(text);
    if (!pay) return true;
    const got = compactText(sniffText(el));
    if (!got) return false;
    if (looksDuplicated(el, text)) return false;
    const probe = pay.slice(0, Math.min(48, pay.length));
    return got.includes(probe) && got.length < Math.floor(pay.length * 1.45) + 32;
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
   *
   * Meta AI (Lexical): execCommand('insertText') already updates the editor.
   * Firing a follow-up InputEvent(inputType=insertFromPaste) stacks a second,
   * often newline-collapsed copy — that is the "prompt twice / half collapsed" bug.
   */
  async function pasteInto(el, text) {
    const value = String(text ?? '');
    el.focus();

    // 1) textarea / input — single value assignment.
    if (setNativeValue(el, value)) return true;

    const isLexical =
      el.getAttribute('data-lexical-editor') === 'true' ||
      !!el.closest('[data-lexical-editor="true"]');

    // 2) Lexical hosts: synthetic paste alone (their paste handler owns the model).
    if (isLexical) {
      clearComposer(el);
      await sleep(0);
      if (dispatchSyntheticPaste(el, value)) {
        await sleep(40);
        return true;
      }
      // Fall through to insertText without a second input event.
    }

    // 3) Clear, then ONE contenteditable strategy.
    clearComposer(el);
    await sleep(0);

    // Prefer insertText: replaces selection, keeps \n, widely supported in Chrome CE.
    try {
      selectAllIn(el);
      if (document.queryCommandSupported && !document.queryCommandSupported('insertText')) {
        /* fall through */
      } else if (document.execCommand('insertText', false, value)) {
        // Do not fireInput — insertText already mutated the editing host.
        // Extra insertFromPaste events double-insert on Lexical/Meta.
        return true;
      }
    } catch {
      /* ignore */
    }

    // 4) Synthetic paste only (no further methods after this succeeds at dispatch).
    clearComposer(el);
    await sleep(0);
    if (dispatchSyntheticPaste(el, value)) {
      // Give Lexical a frame to apply; do not chain more inserts / fireInput.
      await sleep(40);
      return true;
    }

    // 5) insertHTML with <br>
    clearComposer(el);
    try {
      selectAllIn(el);
      if (document.execCommand('insertHTML', false, plainToHtml(value))) {
        // insertHTML already applied; mild input notify only if not Lexical.
        if (!isLexical) fireInput(el, value, 'insertText');
        return true;
      }
    } catch {
      /* ignore */
    }

    // 6) DOM fragment
    clearComposer(el);
    if (insertMultilineFragment(el, value)) return true;

    // 7) last resort
    try {
      el.innerHTML = plainToHtml(value);
      if (!isLexical) fireInput(el, value, 'insertText');
      return true;
    } catch {
      return false;
    }
  }

  /** Clear + single insertText (no fireInput). Used when a duplicate is detected. */
  async function repairToSingle(el, text) {
    const value = String(text ?? '');
    clearComposer(el);
    await sleep(30);
    el.focus();
    selectAllIn(el);
    let inserted = false;
    try {
      inserted = !!document.execCommand('insertText', false, value);
    } catch {
      inserted = false;
    }
    if (!inserted) {
      clearComposer(el);
      await sleep(0);
      if (dispatchSyntheticPaste(el, value)) {
        await sleep(40);
        inserted = true;
      }
    }
    if (!inserted) {
      clearComposer(el);
      inserted = insertMultilineFragment(el, value);
    }
    await sleep(120);
    return looksSingleGood(el, value);
  }

  function waitForComposer(timeoutMs) {
    return waitForStableComposer(timeoutMs, { cold: false }).then((r) => r && r.el);
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
          const cold = !!msg.cold;
          const wantOrigin = msg.wantOrigin ? String(msg.wantOrigin) : '';
          const cancelOnFocusSwitch = msg.cancelOnFocusSwitch !== false;
          // Cap form wait at 10s unless caller asked for less.
          const timeoutMs = Math.min(10000, Math.max(500, Number(msg.timeoutMs) || 10000));
          const waited = await waitForStableComposer(timeoutMs, {
            cold,
            wantOrigin,
            cancelOnFocusSwitch,
          });
          if (waited && waited.cancelReason) {
            sendResponse({
              ok: false,
              error: waited.cancelReason,
              href: location.href,
            });
            return;
          }
          const el = waited && waited.el;
          sendResponse({
            ok: !!el,
            detail: el ? `found ${el.tagName} score-stable` : 'no composer',
            href: location.href,
          });
          return;
        }
        if (msg.cmd === 'waitAndPaste') {
          // Default/hard cap 10s so a stuck form never pastes later on the wrong page.
          const timeoutMs = Math.min(10000, Math.max(500, Number(msg.timeoutMs) || 10000));
          const cold = !!msg.cold;
          const wantOrigin = msg.wantOrigin ? String(msg.wantOrigin) : '';
          const cancelOnFocusSwitch = msg.cancelOnFocusSwitch !== false;
          const text = String(msg.text ?? '');
          const nl = (text.match(/\r\n|\n|\r/g) || []).length;
          const t0 = Date.now();
          const opts = { cold, wantOrigin, cancelOnFocusSwitch };

          let waited = await waitForStableComposer(timeoutMs, opts);
          if (waited && waited.cancelReason) {
            sendResponse({
              ok: false,
              error: waited.cancelReason,
              href: location.href,
              cold,
              waitedMs: Date.now() - t0,
            });
            return;
          }
          let el = waited && waited.el;
          if (!el) {
            sendResponse({
              ok: false,
              error: 'composer not found (timeout)',
              href: location.href,
              cold,
              waitedMs: Date.now() - t0,
            });
            return;
          }

          // Re-check focus right before mutating the form.
          const preAbort = pasteAbortReason(opts);
          if (preAbort) {
            sendResponse({
              ok: false,
              error: preAbort,
              href: location.href,
              cold,
              waitedMs: Date.now() - t0,
            });
            return;
          }

          let result = await pasteWithVerify(el, text);
          // One retry after wipe/hydrate (common on cold Gemini open).
          if (!result.ok) {
            let remain = Math.max(0, timeoutMs - (Date.now() - t0));
            if (remain < 400) {
              sendResponse({
                ok: false,
                error: result.detail || 'paste failed (no time to retry)',
                href: location.href,
                cold,
                waitedMs: Date.now() - t0,
              });
              return;
            }
            const midAbort = pasteAbortReason(opts);
            if (midAbort) {
              sendResponse({
                ok: false,
                error: midAbort,
                href: location.href,
                cold,
                waitedMs: Date.now() - t0,
              });
              return;
            }
            const settle = Math.min(cold ? 500 : 250, remain - 100);
            if (settle > 0) await sleep(settle);
            // Recompute budget after settle — never exceed the hard timeout.
            remain = Math.max(0, timeoutMs - (Date.now() - t0));
            if (remain < 200) {
              sendResponse({
                ok: false,
                error: result.detail || 'paste failed (timeout after settle)',
                href: location.href,
                cold,
                waitedMs: Date.now() - t0,
              });
              return;
            }
            waited = await waitForStableComposer(remain, {
              cold: true,
              wantOrigin,
              cancelOnFocusSwitch,
            });
            if (waited && waited.cancelReason) {
              sendResponse({
                ok: false,
                error: waited.cancelReason,
                href: location.href,
                cold,
                waitedMs: Date.now() - t0,
              });
              return;
            }
            el = waited && waited.el;
            if (el) result = await pasteWithVerify(el, text);
          }

          let detail = result.ok
            ? `pasted ${text.length} chars newlines=${nl} ${result.detail || ''}`.trim()
            : result.detail || 'paste failed';

          // Meta/Lexical sometimes still stacks two copies (one collapsed). Detect + repair
          // without firing insertFromPaste again (that re-introduces the double).
          if (result.ok && el && (result.duplicated || looksDuplicated(el, text))) {
            detail += ' WARN:duplicate_detected';
            const repaired = await repairToSingle(el, text);
            if (repaired) {
              detail += '+repaired';
            } else {
              // Second hard attempt after a short settle.
              await sleep(150);
              const repaired2 = await repairToSingle(el, text);
              detail += repaired2 ? '+repaired' : '+repair_failed';
              // Any failed second repair must fail the op (blank/truncated is not OK either).
              if (!repaired2) {
                clearComposer(el);
                result = { ok: false, detail: 'duplicate paste could not be repaired' };
                detail = result.detail;
              }
            }
          }

          // Final sniff: reject still-duplicated content even if earlier steps claimed OK.
          if (result.ok && el && looksDuplicated(el, text)) {
            detail += ' WARN:still_duplicated';
            clearComposer(el);
            result = { ok: false, detail: 'paste left duplicated content' };
            detail = result.detail;
          }

          if (cold) detail += ' cold=1';
          detail += ` waitedMs=${Date.now() - t0}`;

          sendResponse({
            ok: !!result.ok,
            detail,
            error: result.ok ? undefined : result.detail || 'paste failed',
            href: location.href,
            cold,
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
