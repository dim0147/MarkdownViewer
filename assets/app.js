// app.js - the renderer. Receives JSON messages from the C++ host
// (see src/app.h for the protocol) and renders markdown with markdown-it
// (+ task lists, heading anchors) and highlight.js.
//
// Security: markdown-it runs with html:false, so raw HTML in documents is
// escaped and shown literally - no script can be injected by a document.
(() => {
  'use strict';

  // Must mirror the documented defaults in src/settings.h.
  const DEFAULTS = {
    theme: 'auto',          // "auto" | "light" | "dark"
    maxWidth: 920,          // px, 0 = full width
    fontSize: 16,           // px
    syntaxHighlight: true,
    linkify: true,
    typographer: false,
  };

  let cfg = { ...DEFAULTS };
  let md = null;
  let currentName = '';
  const content = document.getElementById('content');

  function buildParser() {
    md = window.markdownit({
      html: false,                 // keep disabled: raw HTML is escaped (see header note)
      linkify: !!cfg.linkify,
      typographer: !!cfg.typographer,
      highlight: (code, lang) => {
        if (!cfg.syntaxHighlight || !lang || !window.hljs.getLanguage(lang)) return '';
        try {
          return window.hljs.highlight(code, { language: lang, ignoreIllegals: true }).value;
        } catch { return ''; }
      },
    })
      .use(window.markdownitTaskLists, { label: false })
      .use(window.markdownItAnchor, { tabIndex: false });
  }

  function applyConfig(rawJson) {
    let user = {};
    try { user = JSON.parse(rawJson); }
    catch (e) { console.warn('config.json is not valid JSON, using defaults:', e); }
    cfg = { ...DEFAULTS, ...user };

    // Theme: flip the vendor stylesheets' media queries.
    const media = {
      light: ['all', 'not all'],
      dark:  ['not all', 'all'],
      auto:  ['(prefers-color-scheme: light)', '(prefers-color-scheme: dark)'],
    }[cfg.theme] || ['(prefers-color-scheme: light)', '(prefers-color-scheme: dark)'];
    for (const [id, idx] of [['css-md-light', 0], ['css-hl-light', 0], ['css-md-dark', 1], ['css-hl-dark', 1]])
      document.getElementById(id).media = media[idx];
    if (cfg.theme === 'light' || cfg.theme === 'dark')
      document.documentElement.dataset.theme = cfg.theme;
    else
      delete document.documentElement.dataset.theme;

    const w = cfg.maxWidth | 0;
    content.style.maxWidth = w > 0 ? w + 'px' : 'none';
    content.style.fontSize = (cfg.fontSize | 0) > 0 ? (cfg.fontSize | 0) + 'px' : '';

    buildParser();
  }

  function render(text, name) {
    const sameDoc = name === currentName;
    const scroll = sameDoc ? window.scrollY : 0;   // keep position on F5
    currentName = name;
    content.innerHTML = md.render(text || '');
    window.scrollTo(0, scroll);
  }

  function showWelcome() {
    currentName = '';
    content.innerHTML = `
      <div class="welcome">
        <div class="logo">M&darr;</div>
        <h1>Markdown Viewer</h1>
        <div class="drop"><b>Drag &amp; drop</b> a .md file here<br><br>
          or press <kbd>Ctrl</kbd>+<kbd>O</kbd> to browse</div>
        <p class="tip">Tip: use <b>Tools &gt; Add to Explorer right-click menu</b> so you can
          right-click any .md file and choose <i>View with Markdown Viewer</i>.</p>
      </div>`;
  }

  // In-page anchor links: <base href="https://viewer.doc/"> would turn "#foo"
  // into a navigation (which the host cancels), so scroll manually instead.
  document.addEventListener('click', (e) => {
    const a = e.target.closest('a[href]');
    if (!a) return;
    const href = a.getAttribute('href');
    if (href && href.startsWith('#')) {
      e.preventDefault();
      const target = document.getElementById(decodeURIComponent(href.slice(1)));
      if (target) target.scrollIntoView();
    }
  });

  window.chrome.webview.addEventListener('message', (e) => {
    const m = e.data;
    if (!m || typeof m !== 'object') return;
    switch (m.type) {
      case 'config':  applyConfig(m.config); break;
      case 'render':  render(m.markdown, m.name); break;
      case 'welcome': showWelcome(); break;
    }
  });

  buildParser();
  window.chrome.webview.postMessage('ready');   // host replies with config + document
})();
