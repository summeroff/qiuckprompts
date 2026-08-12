// Shared paste / tab helpers for the MV3 companion.
// Content script: loaded as a classic script (sets globalThis.qpPaste).
// Background (ESM): import './paste_logic.js' for the same side effect.
// Node tests: require() this file (CJS export).
(function (root) {
  'use strict';

  const ALLOWED_AI_ORIGINS = [
    'https://www.meta.ai',
    'https://meta.ai',
    'https://gemini.google.com',
    'https://grok.com',
    'https://x.com',
    'https://chatgpt.com',
    'https://claude.ai',
    'https://www.perplexity.ai',
    'https://copilot.microsoft.com',
  ];

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

  function compactText(s) {
    return String(s || '').replace(/\s+/g, '');
  }

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

  function looksDuplicatedText(haystack, text) {
    const pay = compactText(text);
    if (pay.length < 20) return false;
    const got = compactText(haystack);
    if (!got) return false;
    if (got.includes(pay + pay)) return true;
    if (countOccurrences(got, pay) >= 2) return true;
    if (got.length >= Math.floor(pay.length * 1.6) && countOccurrences(got, pay.slice(0, 48)) >= 2) {
      return true;
    }
    return false;
  }

  function looksSingleGoodText(haystack, text) {
    const pay = compactText(text);
    if (!pay) return true;
    const got = compactText(haystack);
    if (!got) return false;
    if (looksDuplicatedText(haystack, text)) return false;
    const probe = pay.slice(0, Math.min(48, pay.length));
    return got.includes(probe) && got.length < Math.floor(pay.length * 1.45) + 32;
  }

  function textStillPresentText(haystack, text) {
    const pay = compactText(text);
    if (!pay) return true;
    const got = compactText(haystack);
    if (!got) return false;
    const probe = pay.slice(0, Math.min(48, pay.length));
    return got.includes(probe);
  }

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

  function isAllowedAiUrl(url) {
    try {
      const parsed = new URL(String(url || ''));
      return parsed.protocol === 'https:' && ALLOWED_AI_ORIGINS.indexOf(parsed.origin) !== -1;
    } catch {
      return false;
    }
  }

  const api = {
    ALLOWED_AI_ORIGINS: ALLOWED_AI_ORIGINS.slice(),
    escapeHtml,
    plainToHtml,
    compactText,
    countOccurrences,
    looksDuplicatedText,
    looksSingleGoodText,
    textStillPresentText,
    isAlreadyOnApp,
    isAllowedAiUrl,
  };

  if (typeof module === 'object' && module.exports) {
    module.exports = api;
  }
  root.qpPaste = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
