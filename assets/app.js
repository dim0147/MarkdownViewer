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

  let cfg = { ...DEFAULTS };    // the persisted/applied config
  let md = null;
  let currentName = '';
  let currentText = '';         // last rendered source, for live re-render
  const content = document.getElementById('content');

  // Build the markdown-it parser from a config object.
  function buildParser(c) {
    md = window.markdownit({
      html: false,                 // keep disabled: raw HTML is escaped (see header note)
      linkify: !!c.linkify,
      typographer: !!c.typographer,
      highlight: (code, lang) => {
        if (!c.syntaxHighlight || !lang || !window.hljs.getLanguage(lang)) return '';
        try {
          return window.hljs.highlight(code, { language: lang, ignoreIllegals: true }).value;
        } catch { return ''; }
      },
    })
      .use(window.markdownitTaskLists, { label: false })
      .use(window.markdownItAnchor, { tabIndex: false });
  }

  // Apply the page chrome (theme + layout) from a config object.
  function applyStyle(c) {
    const media = {
      light: ['all', 'not all'],
      dark:  ['not all', 'all'],
      auto:  ['(prefers-color-scheme: light)', '(prefers-color-scheme: dark)'],
    }[c.theme] || ['(prefers-color-scheme: light)', '(prefers-color-scheme: dark)'];
    for (const [id, idx] of [['css-md-light', 0], ['css-hl-light', 0], ['css-md-dark', 1], ['css-hl-dark', 1]])
      document.getElementById(id).media = media[idx];
    if (c.theme === 'light' || c.theme === 'dark')
      document.documentElement.dataset.theme = c.theme;
    else
      delete document.documentElement.dataset.theme;

    const w = c.maxWidth | 0;
    content.style.maxWidth = w > 0 ? w + 'px' : 'none';
    content.style.fontSize = (c.fontSize | 0) > 0 ? (c.fontSize | 0) + 'px' : '';
  }

  // ---- mermaid diagrams ----------------------------------------------------
  // ```mermaid fences survive markdown-it as <pre><code class="language-mermaid">
  // (highlight.js has no "mermaid" grammar, so the source passes through escaped
  // but intact). After each render we swap those blocks for SVG that mermaid
  // generates. securityLevel:'strict' keeps document-supplied labels sanitized
  // and disables click/script directives; theme follows the resolved app theme.
  let mermaidSeq = 0;

  function mermaidIsDark(c) {
    if (c.theme === 'dark') return true;
    if (c.theme === 'light') return false;
    return window.matchMedia('(prefers-color-scheme: dark)').matches;   // auto
  }

  function initMermaid(c) {
    if (!window.mermaid) return;
    try {
      window.mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'strict',
        theme: mermaidIsDark(c) ? 'dark' : 'default',
      });
    } catch (e) { console.warn('mermaid init failed:', e); }
  }

  function renderMermaid() {
    if (!window.mermaid) return;
    for (const code of content.querySelectorAll('pre > code.language-mermaid')) {
      const pre = code.parentElement;
      const src = code.textContent;
      const id = 'mmd-' + (++mermaidSeq);
      window.mermaid.render(id, src).then(({ svg }) => {
        if (!pre.isConnected) return;             // doc changed mid-render
        const fig = document.createElement('div');
        fig.className = 'mermaid';
        fig.innerHTML = svg;                      // trusted: strict-mode mermaid output
        pre.replaceWith(fig);
      }).catch((err) => {
        pre.classList.add('mermaid-error');       // leave source visible on parse error
        pre.title = String((err && err.message) || err);
      });
    }
  }

  // Re-render the current document with the current parser (keeps scroll).
  function rerender() {
    if (!currentName) return;
    const scroll = window.scrollY;
    content.innerHTML = md.render(currentText || '');
    window.scrollTo(0, scroll);
    renderMermaid();
  }

  // Adopt a config object: style + parser + (if a doc is open) re-render.
  function applyConfigObj(c) {
    applyStyle(c);
    buildParser(c);
    initMermaid(c);
    rerender();
  }

  function applyConfig(rawJson) {
    let user = {};
    try { user = JSON.parse(rawJson); }
    catch (e) { console.warn('config.json is not valid JSON, using defaults:', e); }
    cfg = { ...DEFAULTS, ...user };
    applyStyle(cfg);
    buildParser(cfg);
    initMermaid(cfg);
    // The host follows a config message with a render/welcome message on load
    // and reload, so no explicit rerender() is needed here.
  }

  function render(text, name) {
    const sameDoc = name === currentName;
    const scroll = sameDoc ? window.scrollY : 0;   // keep position on F5
    currentName = name;
    currentText = text || '';
    content.innerHTML = md.render(currentText);
    window.scrollTo(0, scroll);
    renderMermaid();
  }

  function showWelcome() {
    currentName = '';
    currentText = '';
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

  // ---- Settings panel ------------------------------------------------------
  // A live, in-app editor for config.json. Changes preview instantly; "Save"
  // posts the JSON back to the host ("save:<json>"), which writes config.json.
  const settings = (() => {
    let draft = null;            // working copy while the panel is open
    let ui = null;               // { overlay, controls... }, built lazily

    const build = () => {
      const overlay = el('div', 'mv-overlay', { hidden: true });
      const panel = el('div', 'mv-panel');
      panel.append(el('h2', 'mv-title', { text: 'Settings' }));

      const theme = control(panel, 'Theme', selectEl([
        ['auto', 'Auto (match Windows)'], ['light', 'Light'], ['dark', 'Dark'],
      ]));
      const width = slider(panel, 'Content width', 0, 1600, 20);
      const font  = slider(panel, 'Font size', 11, 28, 1);
      const syntax = toggle(panel, 'Syntax highlighting');
      const linkify = toggle(panel, 'Auto-link bare URLs');
      const typo = toggle(panel, 'Smart typography (quotes, dashes)');

      const footer = el('div', 'mv-footer');
      const reset = button(footer, 'Reset to defaults', 'mv-btn');
      const spacer = el('span', 'mv-spacer'); footer.append(spacer);
      const cancel = button(footer, 'Cancel', 'mv-btn');
      const save = button(footer, 'Save', 'mv-btn mv-btn-primary');
      panel.append(footer);
      overlay.append(panel);
      document.body.append(overlay);

      // Live preview: mutate draft and re-apply on every input.
      const live = () => applyConfigObj(draft);
      theme.input.addEventListener('change', () => { draft.theme = theme.input.value; live(); });
      width.input.addEventListener('input', () => {
        draft.maxWidth = +width.input.value;
        width.value.textContent = draft.maxWidth ? draft.maxWidth + ' px' : 'Full width';
        live();
      });
      font.input.addEventListener('input', () => {
        draft.fontSize = +font.input.value;
        font.value.textContent = draft.fontSize + ' px';
        live();
      });
      syntax.input.addEventListener('change', () => { draft.syntaxHighlight = syntax.input.checked; live(); });
      linkify.input.addEventListener('change', () => { draft.linkify = linkify.input.checked; live(); });
      typo.input.addEventListener('change', () => { draft.typographer = typo.input.checked; live(); });

      reset.addEventListener('click', () => { draft = { ...draft, ...DEFAULTS }; populate(); live(); });
      cancel.addEventListener('click', close);
      save.addEventListener('click', commit);
      overlay.addEventListener('mousedown', (e) => { if (e.target === overlay) close(); });

      ui = { overlay, theme, width, font, syntax, linkify, typo };
    };

    // Push draft values into the controls.
    const populate = () => {
      ui.theme.input.value = ['auto', 'light', 'dark'].includes(draft.theme) ? draft.theme : 'auto';
      ui.width.input.value = draft.maxWidth | 0;
      ui.width.value.textContent = (draft.maxWidth | 0) ? (draft.maxWidth | 0) + ' px' : 'Full width';
      ui.font.input.value = (draft.fontSize | 0) || DEFAULTS.fontSize;
      ui.font.value.textContent = ((draft.fontSize | 0) || DEFAULTS.fontSize) + ' px';
      ui.syntax.input.checked = !!draft.syntaxHighlight;
      ui.linkify.input.checked = !!draft.linkify;
      ui.typo.input.checked = !!draft.typographer;
    };

    const open = () => {
      if (!ui) build();
      draft = { ...cfg };        // preserves any unknown keys present in cfg
      populate();
      ui.overlay.hidden = false;
    };

    const close = () => {
      if (!ui || ui.overlay.hidden) return;
      ui.overlay.hidden = true;
      applyConfigObj(cfg);       // revert any unsaved live-preview changes
    };

    const commit = () => {
      cfg = { ...draft };
      applyConfigObj(cfg);
      window.chrome.webview.postMessage('save:' + JSON.stringify(cfg, null, 2));
      ui.overlay.hidden = true;
    };

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && ui && !ui.overlay.hidden) { e.preventDefault(); close(); }
    });

    return { open };
  })();

  // ---- tiny DOM helpers (no innerHTML: stays clear of the CSP/XSS surface) --
  function el(tag, cls, opts) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (opts && opts.text != null) n.textContent = opts.text;
    if (opts && opts.hidden) n.hidden = true;
    return n;
  }
  function control(parent, labelText, inputNode) {
    const row = el('div', 'mv-row');
    const label = el('label', 'mv-label', { text: labelText });
    row.append(label, inputNode);
    parent.append(row);
    return { row, input: inputNode };
  }
  function selectEl(pairs) {
    const s = el('select', 'mv-select');
    for (const [val, text] of pairs) {
      const o = el('option', null, { text });
      o.value = val;
      s.append(o);
    }
    return s;
  }
  function slider(parent, labelText, min, max, step) {
    const row = el('div', 'mv-row');
    const label = el('label', 'mv-label', { text: labelText });
    const input = el('input', 'mv-range');
    input.type = 'range'; input.min = min; input.max = max; input.step = step;
    const value = el('span', 'mv-value');
    const wrap = el('div', 'mv-slider'); wrap.append(input, value);
    row.append(label, wrap);
    parent.append(row);
    return { row, input, value };
  }
  function toggle(parent, labelText) {
    const row = el('div', 'mv-row mv-row-toggle');
    const label = el('label', 'mv-label', { text: labelText });
    const input = el('input', 'mv-check');
    input.type = 'checkbox';
    row.append(label, input);
    parent.append(row);
    return { row, input };
  }
  function button(parent, text, cls) {
    const b = el('button', cls, { text });
    b.type = 'button';
    parent.append(b);
    return b;
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
      case 'config':   applyConfig(m.config); break;
      case 'render':   render(m.markdown, m.name); break;
      case 'welcome':  showWelcome(); break;
      case 'settings': settings.open(); break;
    }
  });

  buildParser(cfg);
  initMermaid(cfg);
  window.chrome.webview.postMessage('ready');   // host replies with config + document
})();
