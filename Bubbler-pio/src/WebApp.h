#pragma once

// The single-page app shell: layout, CSS, and the JS app logic. Served once
// at GET /. Everything after that is client-side tab switching (fetching
// server-rendered HTML fragments from /fragment/*) and fetch-based actions
// (posting to /action/* and /api/status) - no further full-page loads.
//
// Generic wiring conventions used by fragments (see WebUI.cpp's render*
// functions): a <form data-live='/action/...'> applies on every input/change
// (no explicit submit needed - used for effect params and mic sliders); a
// <form data-save='/action/...'> only applies on submit (used wherever a
// user-typed name is required, e.g. saving a palette or preset); a form can
// have both attributes (e.g. mic settings: sliders live-apply, the submit
// button additionally persists to flash). Elements with data-action post to
// that URL with no body and then reload the current tab (or data-goto-tab,
// if set) - e.g. activating an effect/preset, deleting a palette/preset.
// Elements with data-edit fetch that fragment URL in place without changing
// the active tab (used for the palette edit-in-place link).
static const char SHELL_HTML[] = R"HTML(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>
<title>Bubbler Totem</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#111;color:#eee;-webkit-tap-highlight-color:transparent}
#statusbar{position:sticky;top:0;z-index:10;display:flex;align-items:center;justify-content:center;gap:0.5em;padding:calc(0.6em + env(safe-area-inset-top)) 1em 0.6em;background:#1a1a1a;border-bottom:1px solid #2a2a2a;font-size:0.95em}
#statusbar #sbDot{width:0.6em;height:0.6em;border-radius:50%;background:#555;flex-shrink:0}
#statusbar #sbDot.on{background:#4caf50}
#statusbar #sbBpm{color:#999}
#content{padding:1em 1em calc(5.5em + env(safe-area-inset-bottom));max-width:480px;margin:0 auto}
#tabbar{position:fixed;left:0;right:0;bottom:0;z-index:10;display:flex;background:#1a1a1a;border-top:1px solid #2a2a2a;padding-bottom:env(safe-area-inset-bottom)}
#tabbar button{flex:1;background:none;border:none;color:#999;font-size:0.7em;padding:0.6em 0;display:flex;flex-direction:column;align-items:center;gap:0.15em;cursor:pointer;font-family:inherit}
#tabbar button .ic{font-size:1.3em;line-height:1}
#tabbar button.active{color:#4caf50}
h1,h2{margin:0.3em 0 0.6em;font-size:1.1em;color:#ccc}
a.button,button.button{display:block;width:100%;margin:0.4em 0;padding:0.8em;min-height:44px;border-radius:10px;background:#262626;color:#fff;text-decoration:none;font-size:1em;border:none;text-align:center;font-family:inherit}
a.button.active,button.button.active{background:#4caf50}
form{margin:0.8em 0}
form label{display:block;margin:0.8em 0;font-size:0.9em;color:#ccc}
input[type=range],input[type=text],select{width:100%;margin-top:0.3em}
input[type=color]{width:2.2em;height:2.2em;border:none;margin:0.2em;padding:0;background:none}
input[type=submit],.tbtn{min-height:40px;padding:0.6em 1.2em;border-radius:8px;background:#333;color:#fff;border:none;font-size:0.95em;cursor:pointer;font-family:inherit}
input[type=submit]{width:100%;margin-top:0.6em;background:#4caf50}
.link{color:#8cf;text-decoration:none;display:inline-block;margin:0.3em 0}
#vu{width:100%;height:20px;background:#262626;border-radius:4px;margin:0.6em 0;overflow:hidden}
#vuBar{height:100%;width:0%;background:#4caf50}
#beatDot{width:20px;height:20px;border-radius:50%;background:#262626;margin:0.4em auto}
#beatDot.on{background:#ff5252}
.band{width:100%;height:14px;background:#262626;border-radius:4px;margin:0.3em 0;overflow:hidden}
.band>span{display:block;height:100%;width:0%}
#bassBar{background:#ff5252}#midBar{background:#4caf50}#trebBar{background:#8cf}
#bpmReadout{font-size:1.6em;margin:0.2em 0}#confReadout{color:#999;font-size:0.85em}
.tempoRow{display:flex;gap:0.5em;margin:0.6em 0}
.tempoRow .tbtn{flex:1}
.swatches{display:flex;gap:0.25em;flex-wrap:wrap}
.swatches span{width:1.5em;height:1.5em;border-radius:4px;display:inline-block;border:1px solid #444}
.row{margin:0.5em 0;padding:0.6em;background:#1c1c1c;border-radius:10px;display:flex;align-items:center;gap:0.5em}
.row.active{outline:2px solid #4caf50}
.row>div:first-child{flex:1;min-width:0}
.row small{color:#999}
.stub{color:#999;text-align:center;padding:2em 1em;line-height:1.5}
section+section,h2{margin-top:1.4em;padding-top:0.8em;border-top:1px solid #262626}
h1+*,h1{border-top:none;padding-top:0;margin-top:0.3em}
</style>
</head><body>
<div id='statusbar'><span id='sbDot'></span><span id='sbEffect'>--</span><span id='sbBpm'></span></div>
<div id='content'></div>
<nav id='tabbar'>
<button data-tab='mode'><span class='ic'>&#127899;</span>Mode</button>
<button data-tab='effects'><span class='ic'>&#10024;</span>Effects</button>
<button data-tab='saved'><span class='ic'>&#11088;</span>Saved</button>
<button data-tab='palettes'><span class='ic'>&#127912;</span>Palettes</button>
<button data-tab='settings'><span class='ic'>&#9881;</span>Settings</button>
</nav>
<script>
(function(){
  var content = document.getElementById('content');
  var tabButtons = Array.prototype.slice.call(document.querySelectorAll('#tabbar button'));
  var currentTab = null;

  function qs(sel, root) { return (root || document).querySelector(sel); }
  function qsa(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  function debounce(fn, ms) {
    var t;
    return function() { clearTimeout(t); t = setTimeout(fn, ms); };
  }

  function postForm(url, form) {
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams(new FormData(form)).toString()
    });
  }

  function wireContent() {
    qsa('form', content).forEach(function(form) {
      if (form.enctype === 'multipart/form-data') return;
      var liveUrl = form.getAttribute('data-live');
      var saveUrl = form.getAttribute('data-save');
      if (liveUrl) {
        var send = debounce(function() { postForm(liveUrl, form); }, 80);
        form.addEventListener('input', send);
        form.addEventListener('change', send);
      }
      form.addEventListener('submit', function(e) {
        e.preventDefault();
        if (saveUrl) {
          postForm(saveUrl, form).then(function() { loadTab(currentTab); });
        } else if (liveUrl) {
          postForm(liveUrl, form);
        }
      });
    });

    qsa('[data-action]', content).forEach(function(el) {
      el.addEventListener('click', function(e) {
        e.preventDefault();
        if (el.dataset.confirm && !confirm(el.dataset.confirm)) return;
        fetch(el.getAttribute('data-action'), { method: 'POST' }).then(function() {
          loadTab(el.dataset.gotoTab || currentTab);
        });
      });
    });

    qsa('[data-edit]', content).forEach(function(el) {
      el.addEventListener('click', function(e) {
        e.preventDefault();
        fetchFragment(el.getAttribute('data-edit'));
      });
    });

    var uf = qs('#updateForm', content);
    if (uf) {
      uf.addEventListener('submit', function(e) {
        e.preventDefault();
        var status = qs('#updateStatus', uf);
        status.textContent = 'Uploading...';
        fetch('/action/update', { method: 'POST', body: new FormData(uf) })
          .then(function(r) { return r.text(); })
          .then(function(t) { status.textContent = t; })
          .catch(function() { status.textContent = 'Upload failed'; });
      });
    }
  }

  function fetchFragment(path) {
    return fetch(path).then(function(r) { return r.text(); }).then(function(html) {
      content.innerHTML = html;
      wireContent();
      refreshStatus();
    });
  }

  function loadTab(name) {
    currentTab = name;
    tabButtons.forEach(function(b) { b.classList.toggle('active', b.dataset.tab === name); });
    return fetchFragment('/fragment/' + name);
  }

  function refreshStatus() {
    var url = '/api/status' + (currentTab === 'settings' ? '?mic=1' : '');
    return fetch(url).then(function(r) { return r.json(); }).then(function(d) {
      qs('#sbEffect').textContent = d.presetName || d.effectName || '--';
      qs('#sbBpm').textContent = d.bpm > 0 ? (Math.round(d.bpm) + ' BPM') : '';
      qs('#sbDot').classList.toggle('on', d.beat);

      var vu = document.getElementById('vuBar'); if (vu) vu.style.width = (d.level * 100) + '%';
      var bd = document.getElementById('beatDot'); if (bd) bd.className = d.beat ? 'on' : '';
      var bb = document.getElementById('bassBar'); if (bb) bb.style.width = (d.bass * 100) + '%';
      var mb = document.getElementById('midBar'); if (mb) mb.style.width = (d.mid * 100) + '%';
      var tb = document.getElementById('trebBar'); if (tb) tb.style.width = (d.treble * 100) + '%';
      var bpmEl = document.getElementById('bpmReadout'); if (bpmEl) bpmEl.textContent = (d.bpm > 0 ? Math.round(d.bpm) : '--') + ' BPM';
      var confEl = document.getElementById('confReadout'); if (confEl) confEl.textContent = 'lock: ' + Math.round(d.conf * 100) + '%';
    }).catch(function() {});
  }

  function scheduleStatusPoll() {
    refreshStatus().then(function() {
      setTimeout(scheduleStatusPoll, currentTab === 'settings' ? 150 : 1000);
    });
  }

  tabButtons.forEach(function(b) {
    b.addEventListener('click', function() { loadTab(b.dataset.tab); });
  });

  window.__tapTempo = function() { fetch('/action/mic/tap', { method: 'POST' }); };
  window.__nudgeTempo = function(dir) { fetch('/action/mic/nudge?dir=' + dir, { method: 'POST' }); };

  loadTab('effects');
  scheduleStatusPoll();
})();
</script>
</body></html>
)HTML";
