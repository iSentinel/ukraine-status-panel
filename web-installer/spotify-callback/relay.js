/* Static HTTPS callback. Only the non-secret readiness message uses "*". */
"use strict";
const STATE = /^[A-Za-z0-9_-]{22}$/;
const SAFE = /^[\x20-\x7e]*$/;
const language = (navigator.language || "en").slice(0, 2);
const text = {
  en: {manualToggle:"Return manually", title:"Spotify authorization", wait:"Returning the result to your panel web page…",
    invalid:"Invalid or expired return. Restart from the panel web page.",
    manual:"Copy this result into the panel web page.",
    copy:"Copy result", copied:"Result copied. Paste it into the panel web page.",
    sent:"Authorization result returned. You can close this window.",
    privacy:"No password or verification secret is stored on this page."},
  ru: {manualToggle:"Перенести вручную", title:"Авторизация Spotify", wait:"Возвращаем результат на веб-страницу панели…",
    invalid:"Результат неверен или устарел. Начните заново на веб-странице панели.",
    manual:"Скопируйте результат и вставьте его на веб-странице панели.",
    copy:"Копировать результат", copied:"Результат скопирован. Вставьте его на странице панели.",
    sent:"Результат передан. Это окно можно закрыть.",
    privacy:"Пароль и секрет проверки на этой странице не хранятся."},
  uk: {manualToggle:"Перенести вручну", title:"Авторизація Spotify", wait:"Повертаємо результат на вебсторінку панелі…",
    invalid:"Результат хибний або застарілий. Почніть знову на вебсторінці панелі.",
    manual:"Скопіюйте результат і вставте його на вебсторінці панелі.",
    copy:"Копіювати результат", copied:"Результат скопійовано. Вставте його на сторінці панелі.",
    sent:"Результат передано. Це вікно можна закрити.",
    privacy:"Пароль і секрет перевірки на цій сторінці не зберігаються."}
}[language] || null;
const words = text || {
  manualToggle:"Return manually", title:"Spotify authorization", wait:"Returning the authorization result…",
  invalid:"Restart authorization from the panel web page.",
  manual:"Copy this result into the panel web page.", copy:"Copy result",
  copied:"Result copied.", sent:"Result returned. You can close this window.",
  privacy:"No password or verification secret is stored on this page."
};
document.documentElement.lang = text ? language : "en";
for (const [id, key] of [["manual-return","manualToggle"],["title","title"],["manual-label","manual"],["copy","copy"],["privacy","privacy"]])
  document.getElementById(id).textContent = words[key];
const note = value => { document.getElementById("status").textContent = value; };

function manualResult(result) {
  const section = document.getElementById("manual"), field = document.getElementById("result");
  field.value = JSON.stringify({type:"usp-spotify-callback", ...result});
  section.hidden = false; note(words.manual);
  document.getElementById("copy").addEventListener("click", async () => {
    field.select();
    try { await navigator.clipboard.writeText(field.value); note(words.copied); }
    catch { note(words.manual); }
  });
  setTimeout(() => {
    field.value = ""; section.hidden = true; note(words.invalid);
  }, 60000);
}

function localOrigin(origin) {
  try {
    const u = new URL(origin);
    if (u.origin !== origin || u.protocol !== "http:" || u.username || u.password)
      return false;
    const host = u.hostname;
    if (host.length > 6 && host.endsWith(".local") && host.split(".").every(p => /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/i.test(p))) return true;
    if (/^\[(?:fd|fc|fe80:)[a-f0-9:]+\]$/i.test(host) || host === "[::1]")
      return true;
    const pieces = host.split(".");
    if (pieces.length !== 4 || !pieces.every(p => /^\d{1,3}$/.test(p) && +p <= 255))
      return false;
    const [a, b] = pieces.map(Number);
    return a === 10 || a === 127 || (a === 172 && b >= 16 && b <= 31) ||
      (a === 192 && b === 168) || (a === 169 && b === 254);
  } catch { return false; }
}

function readResult(search) {
  const params = new URLSearchParams(search);
  if ([...params.keys()].some(k => !["code", "error", "state"].includes(k)))
    return null;
  if (params.getAll("state").length !== 1 || params.getAll("code").length > 1 ||
      params.getAll("error").length > 1) return null;
  const state = params.get("state"), code = params.get("code"), error = params.get("error");
  if (!STATE.test(state || "") || (code !== null) === (error !== null)) return null;
  const value = code === null ? error : code, limit = code === null ? 128 : 1024;
  if (!value || value.length > limit || !SAFE.test(value)) return null;
  return code === null ? {state, error} : {state, code};
}

const search = location.search, hash = location.hash;
// Remove provider values before messaging, including on malformed input.
history.replaceState(null, "", location.pathname);
let payload = !hash && location.protocol === "https:" ? readResult(search) : null;
const openerWindow = window.opener;
let finished = false;
let readyTimer = 0, expiryTimer = 0;
const clean = () => {
  finished = true; payload = null;
  document.getElementById("manual-return").hidden = true;
  clearInterval(readyTimer); clearTimeout(expiryTimer);
  window.removeEventListener("message", receive);
};
function receive(event) {
  if (finished || event.source !== openerWindow || !localOrigin(event.origin)) return;
  const data = event.data;
  if (!data || data.type !== "usp-spotify-bind" || data.state !== payload.state) return;
  const result = {type: "usp-spotify-callback", ...payload};
  // Stop before sending so a repeated bind cannot replay a result.
  clean();
  openerWindow.postMessage(result, event.origin);
  note(words.sent);
}
function ready() {
  if (openerWindow.closed) {
    const result = payload; clean(); manualResult(result); return;
  }
  openerWindow.postMessage({type: "usp-spotify-relay-ready"}, "*");
}
function initializeReturn() {
  if (!payload || window.top !== window) {
    payload = null; note(words.invalid); return;
  }
  if (!openerWindow || openerWindow.closed) {
    manualResult(payload); payload = null; return;
  }
  note(words.wait);
  const manualButton = document.getElementById("manual-return");
  manualButton.hidden = false;
  manualButton.addEventListener("click", () => {
    if (finished || !payload) return;
    const result = payload;
    clean();
    manualResult(result);
  }, {once:true});
  window.addEventListener("message", receive);
  readyTimer = setInterval(ready, 500);
  expiryTimer = setTimeout(() => {
    clean(); note(words.invalid);
  }, 60000);
  ready();
}
initializeReturn();
