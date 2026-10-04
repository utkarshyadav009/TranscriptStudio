// Transcript Studio — interface logic.
// Native functions are exposed by the C++ host as window.ts_<name>(...) and return promises.
// The host calls back into window.TS.on<Event>(...).
"use strict";

const $ = (id) => document.getElementById(id);
const native = new Proxy({}, {
  get: (_, name) => async (...args) => {
    const fn = window["ts_" + name];
    if (!fn) throw new Error("native function missing: " + name);
    return fn(...args);
  },
});

const S = {
  lang: "en", reviewer: "", library: [], project: null, path: null,
  current: -1, word: -1, pos: 0, dur: 0, playing: false, audio: false,
  query: "", onlyCheck: false, follow: true, lastScroll: 0, autoScrollAt: 0, saveTimer: 0, editing: null,
  peaks: null, wavePeaksW: 0,
};

// ---------------------------------------------------------------- helpers
function t(key, vars) {
  let s = (I18N[S.lang] && I18N[S.lang][key]) || I18N.en[key] || key;
  if (vars) for (const k in vars) s = s.replaceAll("{" + k + "}", vars[k]);
  return s;
}
function esc(s) {
  return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}
function fmt(sec, hours) {
  sec = Math.max(0, sec || 0);
  const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = Math.floor(sec % 60);
  const p = (n) => String(n).padStart(2, "0");
  return hours || h ? `${h}:${p(m)}:${p(s)}` : `${p(m)}:${p(s)}`;
}
function squash(s) { return s.replace(/\s+/g, " ").trim(); }
function toast(msg, err, ms = 6000) {
  const el = $("toast");
  el.textContent = msg;
  el.className = "toast" + (err ? " err" : "");
  el.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (el.hidden = true), ms);
}
function speakerById(id) {
  return (S.project && S.project.speakers.find((s) => s.id === id)) || { id, name: t("unknownSpeaker"), color: "#6B6F76" };
}
function hoursMode() { return (S.project && S.project.duration >= 3600) || false; }

// ---------------------------------------------------------------- language
function applyI18n() {
  document.documentElement.lang = S.lang;
  document.querySelectorAll("[data-i18n]").forEach((el) => (el.textContent = t(el.dataset.i18n)));
  document.querySelectorAll("[data-i18n-ph]").forEach((el) => (el.placeholder = t(el.dataset.i18nPh)));
  document.querySelectorAll("[data-i18n-title]").forEach((el) => (el.title = t(el.dataset.i18nTitle)));
  document.querySelectorAll(".lang button").forEach((b) => b.classList.toggle("on", b.dataset.lang === S.lang));
  updatePlayButton();
}
function setLang(lang) {
  S.lang = lang;
  applyI18n();
  renderAll();
  native.settings({ uiLang: lang });
}

// ---------------------------------------------------------------- library (left column)
function renderLibrary() {
  const ul = $("library");
  if (!S.library.length) {
    ul.innerHTML = `<li class="empty-note" style="cursor:default">${esc(t("noRecordings"))}</li>`;
    return;
  }
  ul.innerHTML = S.library.map((p) => `
    <li data-path="${esc(p.path)}" class="${p.path === S.path ? "on" : ""}">
      <span class="t">${esc(p.title)}</span>
      <span class="m"><span>${fmt(p.duration, p.duration >= 3600)}</span><span>${esc(p.created.slice(0, 10))}</span>
      ${p.to_check ? `<span class="badge">${esc(t("toCheckCount", { n: p.to_check }))}</span>` : ""}</span>
    </li>`).join("");
}
$("library").addEventListener("click", (e) => {
  const li = e.target.closest("li[data-path]");
  if (li && li.dataset.path !== S.path) openProject(li.dataset.path);
});

function renderMeta() {
  const p = S.project;
  $("meta").hidden = !p;
  if (!p) return;
  const words = p.turns.reduce((n, tr) => n + (tr.text ? tr.text.split(/\s+/).length : 0), 0);
  const audioName = (p.audio || "").split(/[\\/]/).pop();
  const lang = p.language ? p.language.toUpperCase() : "—";
  $("metaBody").innerHTML = `
    <dl>
      <dt>${esc(t("duration"))}</dt><dd>${fmt(p.duration, hoursMode())}</dd>
      <dt>${esc(t("language"))}</dt><dd>${esc(lang)}</dd>
      <dt>${esc(t("speakers"))}</dt><dd>${p.speakers.filter((s) => s.id).length}</dd>
      <dt>${esc(t("turns"))}</dt><dd>${p.turns.length}</dd>
      <dt>${esc(t("words"))}</dt><dd>${words}</dd>
      <dt>${esc(t("created"))}</dt><dd>${esc((p.created || "").replace("T", " ").slice(0, 16))}</dd>
      <dt>${esc(t("processedOn"))}</dt><dd title="${esc(p.engine && p.engine.device || "")}">${esc(p.engine && p.engine.device || "—")}</dd>
      <dt>${esc(t("audioFile"))}</dt><dd title="${esc(p.audio)}">${esc(audioName)}</dd>
    </dl>
    ${S.audio ? "" : `<p class="hint" style="color:var(--check)">${esc(t("audioMissing"))}</p>`}
    <div class="links">
      ${S.audio ? `<button class="btn" id="mReveal">${esc(t("showFile"))}</button>`
                : `<button class="btn" id="mFind">${esc(t("findAudio"))}</button>`}
      <button class="btn" id="mDelete">${esc(t("deleteRecording"))}</button>
    </div>`;
  const r = $("mReveal"), f = $("mFind");
  if (r) r.onclick = () => native.reveal(p.audio);
  if (f) f.onclick = async () => {
    const res = await native.relocateAudio();
    if (res && res.path) { p.audio = res.path; scheduleSave(); renderMeta(); }
  };
  $("mDelete").onclick = async () => {
    if (!confirm(t("confirmDelete", { title: p.title }))) return;
    await native.deleteProject(S.path);
    S.project = null; S.path = null; S.audio = false;
    await refreshLibrary();
    renderAll();
  };
}

// ---------------------------------------------------------------- timeline (centre)
function turnText(tr, k) {
  const q = S.query.trim();
  if (q) {
    const re = new RegExp(q.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
    return esc(tr.text).replace(re, (m) => `<mark>${m}</mark>`);
  }
  if (tr.words && tr.words.length && tr.text === tr.orig) {
    return tr.words.map((w, i) => {
      const low = w[3] < 0.5;
      return `<span class="w${low ? " low" : ""}" data-w="${i}"${low ? ` title="${esc(t("lowConfidence"))}"` : ""}>${esc(w[0])}</span>`;
    }).join(" ");
  }
  return esc(tr.text);
}
function turnHTML(tr, k) {
  const spk = speakerById(tr.spk);
  const st = tr.status || "auto";
  const mark = st === "check" ? "⚑" : "✓";
  const title = st === "ok" ? t("markToCheck") : t("markChecked");
  return `<article class="turn ${st}${k === S.current ? " current" : ""}" data-k="${k}">
    <button class="ts" title="${esc(t("playFromHere"))}">${fmt(tr.s, hoursMode())}</button>
    <div class="who"><span class="dot" style="--c:${spk.color}"></span><button class="spk" title="${esc(t("changeSpeaker"))}">${esc(spk.name)}</button></div>
    <div class="txt" contenteditable="plaintext-only" spellcheck="true">${turnText(tr, k)}</div>
    <div class="state"><button class="icon" title="${esc(title)}" style="${st === "auto" ? "opacity:.35" : ""}">${mark}</button></div>
  </article>`;
}
function visible(tr) {
  if (S.onlyCheck && tr.status !== "check") return false;
  const q = S.query.trim().toLowerCase();
  if (q && !tr.text.toLowerCase().includes(q) && !speakerById(tr.spk).name.toLowerCase().includes(q)) return false;
  return true;
}
function renderTimeline() {
  const p = S.project;
  $("centerHead").hidden = !p;
  if (!p) {
    $("timeline").innerHTML = `<div class="welcome"><h1>${esc(t("welcomeTitle"))}</h1><p>${esc(t("welcomeText"))}</p></div>`;
    return;
  }
  $("timeline").innerHTML = p.turns.map((tr, k) => (visible(tr) ? turnHTML(tr, k) : "")).join("");
  updateCheckCount();
}
function rerenderTurn(k) {
  const el = document.querySelector(`.turn[data-k="${k}"]`);
  if (!el) return;
  const tmp = document.createElement("div");
  tmp.innerHTML = turnHTML(S.project.turns[k], k);
  el.replaceWith(tmp.firstElementChild);
}
function updateCheckCount() {
  const n = S.project ? S.project.turns.filter((tr) => tr.status === "check").length : 0;
  $("checkCount").textContent = n ? t("toCheckCount", { n }) : "";
}

$("timeline").addEventListener("click", (e) => {
  const art = e.target.closest(".turn");
  if (!art) return;
  const k = +art.dataset.k, tr = S.project.turns[k];
  if (e.target.closest(".ts")) {
    S.follow = true;
    native.play(tr.s);
  } else if (e.target.closest(".spk")) {
    speakerMenu(e.target.closest(".spk"), k);
  } else if (e.target.closest(".state")) {
    tr.status = tr.status === "ok" ? "check" : "ok";
    rerenderTurn(k);
    afterChange();
  } else if (e.target.closest(".w") && (e.altKey || e.ctrlKey || e.metaKey)) {
    native.play(tr.words[+e.target.closest(".w").dataset.w][1]);
  }
});
$("timeline").addEventListener("focusin", (e) => {
  const txt = e.target.closest(".txt");
  if (txt) S.editing = +txt.closest(".turn").dataset.k;
});
$("timeline").addEventListener("focusout", (e) => {
  const txt = e.target.closest(".txt");
  if (!txt) return;
  const k = +txt.closest(".turn").dataset.k, tr = S.project.turns[k];
  S.editing = null;
  const text = squash(txt.innerText);
  if (text !== tr.text) {
    tr.text = text;
    tr.edited = true;
    afterChange();
  }
  rerenderTurn(k);
});
$("timeline").addEventListener("keydown", (e) => {
  if (e.target.closest(".txt") && e.key === "Enter" && !e.ctrlKey && !e.metaKey) {
    e.preventDefault();
    e.target.blur();
  }
});
$("timeline").addEventListener("scroll", () => {
  if (Date.now() - S.autoScrollAt > 300) S.lastScroll = Date.now();
});

function speakerMenu(anchor, k) {
  closePop();
  const pop = document.createElement("div");
  pop.className = "pop";
  const r = anchor.getBoundingClientRect();
  pop.style.left = r.left + "px";
  pop.style.top = r.bottom + 4 + "px";
  pop.innerHTML = S.project.speakers.map((s) =>
    `<button data-id="${s.id}"><span class="dot" style="--c:${s.color}"></span>${esc(s.name)}</button>`).join("") +
    `<button data-id="new">+ ${esc(t("newSpeaker"))}</button>`;
  pop.onclick = (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    let id = b.dataset.id;
    if (id === "new") {
      const name = prompt(t("newSpeakerName"));
      if (!name) return closePop();
      id = Math.max(0, ...S.project.speakers.map((s) => s.id)) + 1;
      S.project.speakers.push({ id, name: squash(name), color: palette(id - 1) });
    }
    const tr = S.project.turns[k];
    tr.spk = +id;
    tr.edited = true;
    closePop();
    rerenderTurn(k);
    renderSpeakers();
    afterChange();
  };
  document.body.appendChild(pop);
  setTimeout(() => document.addEventListener("click", closePop, { once: true }), 0);
}
function closePop() { document.querySelectorAll(".pop").forEach((p) => p.remove()); }
function palette(i) {
  return ["#D9B26A", "#7FA7C9", "#9DBF8F", "#C98F8F", "#8FC1BB", "#C9B38F", "#A7A0C9", "#A0A7B4"][((i % 8) + 8) % 8];
}

// ---------------------------------------------------------------- speakers + checking (right)
function renderSpeakers() {
  const p = S.project;
  $("rightBody").hidden = !p;
  if (!p) return;
  const total = p.turns.reduce((n, tr) => n + Math.max(0, tr.e - tr.s), 0) || 1;
  const stats = {};
  for (const tr of p.turns) {
    const s = (stats[tr.spk] = stats[tr.spk] || { time: 0, turns: 0 });
    s.time += Math.max(0, tr.e - tr.s);
    s.turns++;
  }
  const others = (id) => p.speakers.filter((s) => s.id !== id);
  $("speakers").innerHTML = p.speakers.filter((s) => stats[s.id]).map((s) => {
    const st = stats[s.id], pct = Math.round((100 * st.time) / total);
    return `<li class="speaker" data-id="${s.id}" style="--c:${s.color}">
      <div class="row"><span class="dot"></span><input value="${esc(s.name)}" title="${esc(t("rename"))}"></div>
      <div class="stats"><span>${fmt(st.time, hoursMode())} ${esc(t("talkTime"))}</span><span>${pct}% · ${esc(t("turnsN", { n: st.turns }))}</span></div>
      <div class="bar"><i style="width:${pct}%"></i></div>
      <select><option value="">${esc(t("mergeInto"))}</option>${others(s.id).map((o) => `<option value="${o.id}">${esc(o.name)}</option>`).join("")}</select>
    </li>`;
  }).join("");
  renderChecking();
}
$("speakers").addEventListener("change", (e) => {
  const li = e.target.closest(".speaker");
  if (!li) return;
  const id = +li.dataset.id;
  if (e.target.tagName === "INPUT") {
    const s = speakerById(id), name = squash(e.target.value);
    if (name && name !== s.name) { s.name = name; afterChange(); renderTimeline(); }
  } else if (e.target.tagName === "SELECT" && e.target.value !== "") {
    const into = +e.target.value;
    for (const tr of S.project.turns) if (tr.spk === id) { tr.spk = into; tr.edited = true; }
    S.project.speakers = S.project.speakers.filter((s) => s.id !== id);
    afterChange();
    renderSpeakers();
    renderTimeline();
  }
});

function renderChecking() {
  const p = S.project;
  const total = p.turns.length, done = p.turns.filter((tr) => tr.status === "ok").length;
  const left = p.turns.filter((tr) => tr.status === "check").length;
  $("checking").innerHTML = `
    <div class="mono muted" style="font-size:12px">${esc(t("checkedOf", { done, total }))}</div>
    <div class="progress"><i style="width:${total ? (100 * done) / total : 0}%"></i></div>
    ${left ? `<button class="btn block" id="btnNext">${esc(t("nextToCheck"))} · <span class="mono">${left}</span></button>`
           : `<p class="hint">${esc(t("allChecked"))}</p>`}`;
  const b = $("btnNext");
  if (b) b.onclick = nextToCheck;
}
function nextToCheck() {
  const p = S.project;
  if (!p) return;
  const start = S.current >= 0 ? S.current + 1 : 0;
  for (let i = 0; i < p.turns.length; i++) {
    const k = (start + i) % p.turns.length;
    if (p.turns[k].status === "check") {
      if (S.onlyCheck || S.query) { /* make sure it is visible */ }
      S.follow = true;
      native.playRange(p.turns[k].s, p.turns[k].e + 0.3);
      setCurrent(k, true);
      return;
    }
  }
  toast(t("allChecked"));
}
function markCheckedAndNext() {
  const k = S.editing != null ? S.editing : S.current;
  if (k >= 0 && S.project) {
    const el = document.activeElement;
    if (el && el.classList.contains("txt")) el.blur();
    S.project.turns[k].status = "ok";
    rerenderTurn(k);
    afterChange();
  }
  nextToCheck();
}

// ---------------------------------------------------------------- saving
function afterChange() {
  updateCheckCount();
  renderChecking();
  scheduleSave();
}
function scheduleSave() {
  $("saveState").textContent = t("saving");
  clearTimeout(S.saveTimer);
  S.saveTimer = setTimeout(saveNow, 700);
}
async function saveNow() {
  clearTimeout(S.saveTimer);
  if (!S.project) return;
  const res = await native.saveProject(S.project);
  $("saveState").textContent = res && res.ok ? t("saved") : (res && res.error) || "!";
  const item = S.library.find((x) => x.path === S.path);
  if (item) {
    item.to_check = S.project.turns.filter((tr) => tr.status === "check").length;
    item.title = S.project.title;
    renderLibrary();
  }
}

// ---------------------------------------------------------------- opening
async function refreshLibrary() {
  S.library = (await native.listProjects()) || [];
  renderLibrary();
}
async function openProject(path) {
  if (S.project) await saveNow();
  const res = await native.openProject(path);
  if (!res || !res.ok) return toast((res && res.error) || "?", true);
  S.project = res.project;
  S.path = path;
  S.current = -1;
  S.word = -1;
  S.audio = false;
  S.peaks = null;
  $("docTitle").textContent = S.project.title;
  ["btnWordOpen", "btnWordImport", "btnExport"].forEach((id) => ($(id).disabled = false));
  renderAll();
  $("timeline").scrollTop = 0;
}
function renderAll() {
  renderLibrary();
  renderMeta();
  renderTimeline();
  renderSpeakers();
  drawWave();
}

// ---------------------------------------------------------------- playback
function updatePlayButton() {
  $("btnPlay").innerHTML = S.playing ? `❚❚ <span>${esc(t("pause"))}</span>` : `▶ <span>${esc(t("play"))}</span>`;
  $("btnPlay").disabled = !S.audio;
}
$("btnPlay").onclick = () => (S.playing ? native.pause() : native.play(null));
$("btnBack").onclick = () => native.seek(Math.max(0, S.pos - 5));
$("btnFwd").onclick = () => native.seek(Math.min(S.dur, S.pos + 5));
$("speed").onchange = (e) => native.speed(+e.target.value);

function findTurn(pos) {
  const turns = S.project ? S.project.turns : [];
  let lo = 0, hi = turns.length - 1, ans = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (turns[mid].s <= pos + 0.05) { ans = mid; lo = mid + 1; } else hi = mid - 1;
  }
  return ans;
}
function setCurrent(k, scroll) {
  if (k !== S.current) {
    const old = document.querySelector(".turn.current");
    if (old) old.classList.remove("current");
    S.current = k;
    S.word = -1;
    const el = document.querySelector(`.turn[data-k="${k}"]`);
    if (el) el.classList.add("current");
  }
  if (scroll) scrollToTurn(k);
}
function scrollToTurn(k) {
  const el = document.querySelector(`.turn[data-k="${k}"]`);
  if (!el) return;
  const box = $("timeline"), top = el.offsetTop - box.offsetTop, h = box.clientHeight;
  if (top < box.scrollTop + h * 0.15 || top > box.scrollTop + h * 0.7) {
    S.autoScrollAt = Date.now();
    box.scrollTo({ top: Math.max(0, top - h * 0.25), behavior: "smooth" });
  }
}
function highlightWord(k, pos) {
  const tr = S.project.turns[k];
  if (!tr || !tr.words || tr.text !== tr.orig || S.query) return;
  let w = -1;
  for (let i = 0; i < tr.words.length; i++) if (tr.words[i][1] <= pos) w = i; else break;
  if (w === S.word) return;
  const art = document.querySelector(`.turn[data-k="${k}"]`);
  if (!art) return;
  art.querySelectorAll(".w.now").forEach((x) => x.classList.remove("now"));
  const span = art.querySelector(`.w[data-w="${w}"]`);
  if (span) span.classList.add("now");
  S.word = w;
}
async function tick() {
  if (S.audio) {
    try {
      const st = await native.state();
      S.pos = st.pos;
      S.dur = st.dur;
      if (st.playing !== S.playing) { S.playing = st.playing; updatePlayButton(); }
      $("clock").innerHTML = `<b>${fmt(S.pos, hoursMode())}</b> / ${fmt(S.dur, hoursMode())}`;
      if (S.project && S.editing == null) {
        const k = findTurn(S.pos);
        const userScrolled = Date.now() - S.lastScroll < 4000;
        if (k >= 0) {
          setCurrent(k, S.playing && S.follow && !userScrolled);
          if (S.playing) highlightWord(k, S.pos);
        }
      }
      drawPlayhead();
    } catch (e) { /* host busy */ }
  }
  setTimeout(tick, 60);
}

// ---------------------------------------------------------------- waveform
const wave = $("wave"), base = document.createElement("canvas");
function drawWave() {
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(10, wave.clientWidth), h = Math.max(10, wave.clientHeight);
  wave.width = w * dpr;
  wave.height = h * dpr;
  base.width = wave.width;
  base.height = wave.height;
  const g = base.getContext("2d");
  g.clearRect(0, 0, base.width, base.height);
  if (S.project && S.audio && S.peaks) {
    const laneH = 4 * dpr, mid = (base.height - laneH - 6 * dpr) / 2, amp = mid - 1;
    const cols = S.peaks.length / 2;
    let peak = 0.05;
    for (let i = 0; i < S.peaks.length; i++) peak = Math.max(peak, Math.abs(S.peaks[i]));
    const scale = 1 / Math.min(1, peak * 1.05);
    g.fillStyle = "#4a4e55";
    for (let c = 0; c < cols; c++) {
      const lo = S.peaks[2 * c] * scale, hi = S.peaks[2 * c + 1] * scale;
      const x = (c / cols) * base.width, y0 = mid - hi * amp, y1 = mid - lo * amp;
      g.fillRect(x, y0, Math.max(1, base.width / cols), Math.max(1, y1 - y0));
    }
    // speaker lane
    const dur = S.dur || S.project.duration || 1;
    for (const tr of S.project.turns) {
      g.fillStyle = speakerById(tr.spk).color;
      g.fillRect((tr.s / dur) * base.width, base.height - laneH, Math.max(1, ((tr.e - tr.s) / dur) * base.width), laneH);
    }
  }
  drawPlayhead();
}
function drawPlayhead() {
  const g = wave.getContext("2d");
  g.clearRect(0, 0, wave.width, wave.height);
  g.drawImage(base, 0, 0);
  if (!S.audio || !S.dur) return;
  const x = (S.pos / S.dur) * wave.width;
  // played part brighter
  g.save();
  g.globalCompositeOperation = "source-atop";
  g.fillStyle = "#d8d8d8";
  g.fillRect(0, 0, x, wave.height - 10 * (window.devicePixelRatio || 1));
  g.restore();
  g.fillStyle = "#eeeeee";
  g.fillRect(x, 0, Math.max(1, window.devicePixelRatio || 1), wave.height);
}
async function loadPeaks() {
  const cols = Math.max(200, Math.floor(wave.clientWidth));
  S.peaks = await native.peaks(0, S.dur, cols);
  drawWave();
}
$("waveWrap").addEventListener("click", (e) => {
  if (!S.audio || !S.dur) return;
  const r = wave.getBoundingClientRect();
  const f = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
  S.follow = true;
  S.lastScroll = 0;
  native.seek(f * S.dur);
  setTimeout(() => setCurrent(findTurn(f * S.dur), true), 80);
});
window.addEventListener("resize", () => { clearTimeout(drawWave.t); drawWave.t = setTimeout(() => (S.audio ? loadPeaks() : drawWave()), 150); });

// ---------------------------------------------------------------- Word + export
$("btnWordOpen").onclick = async () => {
  await saveNow();
  const r = await native.openInWord();
  if (r && r.ok) toast(t("wordOpened"), false, 12000); else toast((r && r.error) || "?", true);
};
$("btnWordImport").onclick = async () => {
  await saveNow();
  const r = await native.importWord();
  if (!r || r.cancelled) return;
  if (!r.ok) return toast(r.error, true);
  S.project = r.project;
  renderAll();
  toast(t("imported", r.report), false, 9000);
};
$("btnExport").onclick = (e) => { e.stopPropagation(); $("exportMenu").hidden = !$("exportMenu").hidden; };
document.addEventListener("click", () => ($("exportMenu").hidden = true));
$("expWord").onclick = async () => {
  await saveNow();
  const r = await native.exportWord();
  if (r && r.ok) toast(t("wordSaved", { file: r.path })); else if (r && !r.cancelled) toast(r.error, true);
};
$("expText").onclick = async () => {
  await saveNow();
  const r = await native.exportText();
  if (r && r.ok) toast(t("wordSaved", { file: r.path })); else if (r && !r.cancelled) toast(r.error, true);
};

// ---------------------------------------------------------------- new transcription
let ntPath = null, ntRunning = false;
function ntShow(which) {
  ["ntPick", "ntRun", "ntError"].forEach((id) => ($(id).hidden = id !== which));
  $("ntStart").hidden = which !== "ntPick";
  $("ntCancel").textContent = which === "ntError" ? t("ntClose") : t("ntCancel");
}
$("btnNew").onclick = () => {
  ntPath = null;
  $("ntFile").textContent = "";
  $("ntNameRow").hidden = true;
  $("ntStart").disabled = true;
  ntShow("ntPick");
  $("modal").hidden = false;
};
$("ntChoose").onclick = async () => {
  const r = await native.pickAudio();
  if (!r || !r.path) return;
  ntPath = r.path;
  $("ntFile").textContent = r.name;
  $("ntName").value = r.title;
  $("ntNameRow").hidden = false;
  $("ntStart").disabled = false;
};
$("ntStart").onclick = async () => {
  if (!ntPath) return;
  ntRunning = true;
  ntShow("ntRun");
  TS.onProgress({ stage: "read", fraction: 0 });
  const r = await native.transcribe(ntPath, squash($("ntName").value) || "Recording");
  if (!r || !r.ok) { ntRunning = false; ntFail((r && r.error) || "?"); }
};
$("ntCancel").onclick = () => {
  if (ntRunning) native.cancel();
  ntRunning = false;
  $("modal").hidden = true;
};
function ntFail(msg) {
  $("ntErrText").textContent = msg;
  ntShow("ntError");
}

// ---------------------------------------------------------------- called by the host
window.TS = {
  onProgress(p) {
    const order = ["read", "words", "speakers", "done"];
    const at = order.indexOf(p.stage);
    document.querySelectorAll(".steps li").forEach((li) => {
      const i = order.indexOf(li.dataset.step);
      li.className = i < at ? "done" : i === at ? "on" : "";
    });
    $("ntBar").style.width = Math.round((p.fraction || 0) * 100) + "%";
    const bits = [];
    if (p.device) bits.push(t("using", { device: p.device }));
    if (p.eta > 0) bits.push(t("timeLeft", { t: fmt(p.eta) }));
    $("ntInfo").textContent = bits.join("  ·  ");
  },
  async onDone(res) {
    ntRunning = false;
    if (!res.ok) return res.cancelled ? ($("modal").hidden = true) : ntFail(res.error);
    $("modal").hidden = true;
    await refreshLibrary();
    await openProject(res.path);
  },
  async onAudio(res) {
    S.audio = !!res.ok;
    S.dur = res.duration || 0;
    updatePlayButton();
    renderMeta();
    if (S.audio) await loadPeaks(); else drawWave();
  },
};

// ---------------------------------------------------------------- keyboard
document.addEventListener("keydown", (e) => {
  const typing = e.target.closest && e.target.closest("input, select, textarea, .txt");
  if (e.key === " " && (e.ctrlKey || !typing)) {
    e.preventDefault();
    if (S.audio) S.playing ? native.pause() : native.play(null);
  } else if (e.altKey && (e.key === "ArrowLeft" || e.key === "ArrowRight")) {
    e.preventDefault();
    native.seek(Math.max(0, S.pos + (e.key === "ArrowLeft" ? -5 : 5)));
  } else if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
    e.preventDefault();
    markCheckedAndNext();
  } else if (e.key === "F3") {
    e.preventDefault();
    nextToCheck();
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "f") {
    e.preventDefault();
    $("search").focus();
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
    e.preventDefault();
    saveNow();
  } else if (e.key === "Escape") {
    closePop();
  }
});
$("search").addEventListener("input", (e) => { S.query = e.target.value; renderTimeline(); });
$("onlyCheck").addEventListener("change", (e) => { S.onlyCheck = e.target.checked; renderTimeline(); });
$("reviewer").addEventListener("change", (e) => { S.reviewer = squash(e.target.value); native.settings({ reviewer: S.reviewer }); });
document.querySelectorAll(".lang button").forEach((b) => (b.onclick = () => setLang(b.dataset.lang)));
window.addEventListener("beforeunload", () => saveNow());

// ---------------------------------------------------------------- start
(async function start() {
  const init = await native.init();
  S.lang = init.uiLang || "en";
  S.reviewer = init.reviewer || "";
  $("reviewer").value = S.reviewer;
  applyI18n();
  await refreshLibrary();
  renderAll();
  tick();
  if (init.autotest) new Function(init.autotest)();  // developer self-test hook (TS_AUTOTEST)
})();
