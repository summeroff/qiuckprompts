'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const p = require('../extension/paste_logic.js');

test('escapeHtml encodes markup', () => {
  assert.equal(p.escapeHtml('<script>"&'), '&lt;script&gt;&quot;&amp;');
});

test('plainToHtml keeps newlines as br after escape', () => {
  assert.equal(p.plainToHtml('a\nb'), 'a<br>b');
  assert.equal(p.plainToHtml('<x>\r\ny'), '&lt;x&gt;<br>y');
});

test('looksDuplicatedText ignores short payloads', () => {
  assert.equal(p.looksDuplicatedText('hihihihihihihihihi', 'hi'), false);
});

test('looksDuplicatedText detects stacked full copies', () => {
  const text = 'Please proofread the following draft now.';
  const once = text.replace(/\s+/g, '');
  assert.equal(p.looksDuplicatedText(once, text), false);
  assert.equal(p.looksDuplicatedText(once + once, text), true);
});

test('looksSingleGoodText rejects empty sniff and duplicates', () => {
  const text = 'Please proofread the following draft now.';
  assert.equal(p.looksSingleGoodText('', text), false);
  const once = text.replace(/\s+/g, '');
  assert.equal(p.looksSingleGoodText(once, text), true);
  assert.equal(p.looksSingleGoodText(once + once, text), false);
});

test('textStillPresentText matches a compact prefix', () => {
  const text = 'Please proofread the following draft now.';
  assert.equal(p.textStillPresentText('Please proofread the following draft now. extra', text), true);
  assert.equal(p.textStillPresentText('unrelated composer chrome', text), false);
});

test('isAlreadyOnApp treats origin root as any path', () => {
  const want = new URL('https://www.meta.ai/');
  assert.equal(p.isAlreadyOnApp(new URL('https://www.meta.ai/some/chat'), want), true);
  assert.equal(p.isAlreadyOnApp(new URL('https://gemini.google.com/app'), want), false);
});

test('isAlreadyOnApp accepts path prefix for non-root', () => {
  const want = new URL('https://gemini.google.com/app');
  assert.equal(p.isAlreadyOnApp(new URL('https://gemini.google.com/app/xyz'), want), true);
  assert.equal(p.isAlreadyOnApp(new URL('https://gemini.google.com/other'), want), false);
});

test('isAllowedAiUrl allows listed https origins only', () => {
  assert.equal(p.isAllowedAiUrl('https://gemini.google.com/app'), true);
  assert.equal(p.isAllowedAiUrl('http://gemini.google.com/app'), false);
  assert.equal(p.isAllowedAiUrl('https://evil.example/'), false);
});
