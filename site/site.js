// site.js — tiny enhancement script, ~30 lines.
// Copy buttons + (optional) Prism syntax highlighting (loaded from CDN).
// Site is fully usable with JS disabled; this only adds polish.
(function () {
  'use strict';

  // 1) Copy buttons.
  document.addEventListener('click', function (e) {
    var btn = e.target.closest('.copy-btn');
    if (!btn) return;
    var pre = btn.closest('.code-meta-wrap, .tab-pane, .with-copy');
    if (!pre) pre = btn.parentElement;
    var code = pre.querySelector('pre code');
    if (!code) return;
    var text = code.textContent;
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () {
        btn.classList.add('copied');
        var prev = btn.textContent;
        btn.textContent = 'copied';
        setTimeout(function () {
          btn.classList.remove('copied');
          btn.textContent = prev;
        }, 1400);
      });
    }
  });

  // 2) Mark current top-nav link based on pathname (kept simple).
  try {
    var path = location.pathname.replace(/index\.html$/, '');
    var links = document.querySelectorAll('.topnav-links a');
    links.forEach(function (a) {
      var href = a.getAttribute('href');
      if (!href) return;
      var clean = href.replace(/index\.html$/, '');
      if ((clean !== '' && (path === clean || path.endsWith(clean))) ||
          (clean === '' && (path === '/' || path.endsWith('/queen/') || path.endsWith('/queen')))) {
        a.classList.add('current');
      }
    });
  } catch (_) { /* noop */ }
})();
