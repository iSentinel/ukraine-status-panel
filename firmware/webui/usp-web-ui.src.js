const viewport=node("meta");viewport.name="viewport";viewport.content="width=device-width";document.head.append(viewport);
const GROUPS = [
  ["Setup","setup"],
  ["Panel settings","overview"],
  ["Status & diagnostics","overview"],
  ["Hardware","hardware"],
  ["Location","location"],
  ["SvitloBot","svitlo"],
  ["Network","network"],
  ["Starlink","starlink"],
  ["Shelly","shelly"],
  ["Maintenance","maintenance"],
];
const TAB_ORDER = ["setup", "overview", "location", "hardware", "svitlo", "network", "shelly", "starlink", "spotify", "maintenance"];
const TAB_STORAGE_KEY = "usp-active-tab";
let sessionTab = null;
/** Keep the selected tab usable when optional browser storage is unavailable. */
function tabPreference(value) {
  if (value !== undefined) {
    sessionTab = value;
    try { localStorage.setItem(TAB_STORAGE_KEY, value); } catch {}
  } else {
    try { sessionTab = localStorage.getItem(TAB_STORAGE_KEY) || sessionTab; } catch {}
  }
  return sessionTab;
}
const KYIV_GROUP_API = "https://getnubergroupproxy.onrender.com";
const OUTAGE_SOURCE_LABEL = "SetTimeGenerator/YASNO proxy";
const OUTAGE_GROUP_ENTITY = "Outage Group Code";
const UI_LANGUAGE_ENTITY = "UI Language";
const STARLINK_HOST_ENTITY = "Starlink Host";
const STRINGS={uk:{},ru:{},en:{}};
for(const [k,u,r,e] of [
  ["subtitle","Керування та діагностика","Управление и диагностика","Control & diagnostics"],
  ["connected","Підключено","Подключено","Connected"],
  ["reconnecting","Перепідключення…","Переподключение…","Reconnecting…"],
  ["save","Зберегти","Сохранить","Save"],
  ["saving","Надсилаємо…","Отправляем…","Sending…"],
  ["sent","Запит прийнято","Запрос принят","Request accepted"],
  ["saveError","Не підтверджено. Перевірте стан.","Не подтверждено. Проверьте статус.","Unconfirmed. Check state."],
  ["press","Виконати","Выполнить","Run"],
  ["factoryCredentials","Заводські облікові дані","Заводские учётные данные","Factory credentials"],
  ["customCredentials","Пароль змінено","Пароль изменён","Password changed"],
  ["defaultPassword","Потрібно змінити стандартний пароль","Нужно сменить стандартный пароль","Change the default password"],
  ["on","Увімкнено","Включено","On"],
  ["off","Вимкнено","Выключено","Off"],
  ["tabs",{setup:"Старт", overview:"Огляд", location:"Локація", hardware:"Обладнання", svitlo:"Світло", network:"Мережа", shelly:"Shelly", starlink:"Starlink", spotify:"Spotify", maintenance:"Обслуговування"},{setup:"Старт", overview:"Обзор", location:"Локация", hardware:"Оборудование", svitlo:"Свет", network:"Сеть", shelly:"Shelly", starlink:"Starlink", spotify:"Spotify", maintenance:"Обслуживание"},{setup:"Setup", overview:"Overview", location:"Location", hardware:"Hardware", svitlo:"Power", network:"Network", shelly:"Shelly", starlink:"Starlink", spotify:"Spotify", maintenance:"Maintenance"}],
  ["groups",{Setup:"Перший запуск", Hardware:"Обладнання без пайки", Location:"Локація та погода", "Panel settings":"Налаштування", "Status & diagnostics":"Статус і діагностика", SvitloBot:"Світло / SvitloBot", Network:"Wi-Fi та мережа", Shelly:"Shelly Pro EM-50", Starlink:"Starlink", Maintenance:"Обслуговування"},{Setup:"Первый запуск", Hardware:"Оборудование без пайки", Location:"Локация и погода", "Panel settings":"Настройки", "Status & diagnostics":"Статус и диагностика", SvitloBot:"Свет / SvitloBot", Network:"Wi-Fi и сеть", Shelly:"Shelly Pro EM-50", Starlink:"Starlink", Maintenance:"Обслуживание"},{Setup:"First run", Hardware:"Solderless hardware", Location:"Location & weather", "Panel settings":"Panel settings", "Status & diagnostics":"Status & diagnostics", SvitloBot:"Power / SvitloBot", Network:"Wi-Fi & network", Shelly:"Shelly Pro EM-50", Starlink:"Starlink", Maintenance:"Maintenance"}],
  ["finderTitle","Група відключень за адресою (Київ)","Группа отключений по адресу (Киев)","Outage group by address (Kyiv)"],
  ["street","Вулиця","Улица","Street"],
  ["house","Будинок","Дом","House"],
  ["streetHint","Введіть щонайменше 3 символи","Введите минимум 3 символа","Enter at least 3 characters"],
  ["selectStreet","Оберіть вулицю зі списку","Выберите улицу из списка","Select a street from the list"],
  ["findGroup","Знайти групу","Найти группу","Find group"],
  ["finding","Пошук…","Поиск…","Searching…"],
  ["ag","Застосувати групу","Применить группу","Apply group"],
  ["groupFound","Знайдена група","Найдена группа","Found group"],
  ["groupApplied","Групу застосовано","Группа применена","Group applied"],
  ["lookupError","Не вдалося визначити групу","Не удалось определить группу","Could not determine group"],
  ["externalSource",OUTAGE_SOURCE_LABEL,OUTAGE_SOURCE_LABEL,OUTAGE_SOURCE_LABEL],
  ["externalPrivacy","Для пошуку вулиця й будинок передаються зовнішньому сервісу SetTimeGenerator/YASNO. Згода діє лише до перезавантаження сторінки.","Для поиска улица и дом передаются внешнему сервису SetTimeGenerator/YASNO. Согласие действует только до перезагрузки страницы.","Street and house are sent to the external SetTimeGenerator/YASNO service. Consent lasts only until this page is reloaded."],
  ["enableExternalLookup","Дозволити зовнішній пошук","Разрешить внешний поиск","Enable external lookup"],
  ["setup",["Wi-Fi","Мова","Часовий пояс","Регіон","Погода","Група"],["Wi-Fi","Язык","Часовой пояс","Регион","Погода","Группа"],["Wi-Fi","Language","Time zone","Region","Weather","Outage"]],
  ["open","Відкрити","Открыть","Open"],
  ["spotifySetup","Налаштування Spotify","Настройка Spotify","Spotify setup"],
  ["spotifyHint","Вхід і стан підключення","Вход и состояние подключения","Sign-in and connection status"],
  ["spotifyConnection","Стан підключення","Состояние подключения","Connection state"],
  ["spotifyStorage","Сховище сесії","Хранилище сессии","Session storage"],
  ["spotifyRefresh","Оновити стан","Обновить состояние","Refresh status"],
  ["spotifyManage","Керувати входом","Управлять входом","Manage sign-in"],
  ["spotifyUnknown","Перевіряємо…","Проверяем…","Checking…"],
  ["spotifyIdle","Не підключено","Не подключено","Not connected"],
  ["spotifyPending","Підключення…","Подключение…","Connecting…"],
  ["spotifyConnected","Підключено","Подключено","Connected"],
  ["spotifyReauth","Потрібен повторний вхід","Нужен повторный вход","Sign-in required"],
  ["spotifyError","Стан недоступний","Состояние недоступно","Status unavailable"],
  ["spotifyStorageReady","Захищене сховище готове","Защищённое хранилище готово","Protected storage ready"],
  ["spotifyStorageHardware","Апаратно захищене сховище","Аппаратно защищённое хранилище","Hardware-protected storage"],
  ["spotifyStorageLocal","Локальне збереження готове","Локальное сохранение готово","Local persistence ready"],
  ["spotifyStorageUnprovisioned","Ключ сховища не підготовлено","Ключ хранилища не подготовлен","Storage key not provisioned"],
  ["spotifyStorageBlocked","Ключ сховища заблоковано","Ключ хранилища заблокирован","Storage key blocked"],
  ["spotifyStorageError","Сховище недоступне","Хранилище недоступно","Storage unavailable"],
  ["backupTitle","Копія","Копия","Backup"],
  ["backupNote","JSON без секретів.","JSON без секретов.","JSON excludes secrets."],
  ["exportSafe","Експорт JSON","Экспорт JSON","Export JSON"],
  ["importSafe","Обрати JSON","Выбрать JSON","Choose JSON"],
  ["restoreSafe","Відновити","Восстановить","Restore"],
  ["restoreReady","Готово","Готово","Ready"],
  ["restoreBad","Некоректний файл","Некорректный файл","Invalid file"],
  ["restoreDone","Застосовано","Применено","Applied"],
  ["scanWifi","Сканувати Wi-Fi","Сканировать Wi-Fi","Scan"],
  ["hiddenWifi","Прихована мережа","Скрытая сеть","Hidden network"]
]){STRINGS.uk[k]=u;STRINGS.ru[k]=r;STRINGS.en[k]=e;}
const TAB_INTRO={uk:{hardware:"SHT41/45·бузер"},ru:{hardware:"SHT41/SHT45 (I²C 0x44) и буззер"},en:{hardware:"SHT41/45·buzzer"}};
const LABELS={uk:{},ru:{},en:{}};
for(const [k,u,r] of [
  ["Daily","Щодня","Ежедневно"],
  ["Weekdays","Пн–Пт","Пн–Пт"],
  ["Weekends","Сб–Нд","Сб–Вс"],
  ["Alarm Status","Стан будильників","Состояние будильников"],
  ["Dismiss Alarm","Закрити будильник","Закрыть будильник"],
  ["Panel Backlight","Яскравість","Яркость"],
  [UI_LANGUAGE_ENTITY,"Мова","Язык"],
  ["LCD Mode","Навігація LCD","Навигация LCD"],
  ["Alert Palette","Палітра тривог","Палитра тревог"],
  ["Outage Group","Група відключень","Группа отключений"],
  [OUTAGE_GROUP_ENTITY,"Точна група YASNO","Точная группа YASNO"],
  ["New Web Password","Новий пароль Web UI","Новый пароль Web UI"],
  ["Debug Mode (15 min)","Debug режим (15 хв)","Debug режим (15 мин)"],
  ["Restart","Перезавантаження","Перезагрузка"],
  ["Web Security Status","Безпека Web UI","Безопасность Web UI"],
  ["Firmware Version","Версія прошивки","Версия прошивки"],
  ["Panel IP","IP панелі","IP панели"],
  ["Wi-Fi SSID","Wi-Fi мережа","Wi-Fi сеть"],
  ["Wi-Fi RSSI","Wi-Fi сигнал","Wi-Fi сигнал"],
  ["Uptime","Час роботи","Время работы"],
  ["Free Heap","Вільна heap-пам’ять","Свободная heap-память"],
  ["Free PSRAM","Вільна PSRAM","Свободная PSRAM"],
  ["Panel Loop Stack Headroom","Запас стеку loop","Запас стека loop"],
  ["Panel Battery Voltage","Напруга живлення","Напряжение питания"],
  ["Alert Map Source","Джерело карти тривог","Источник карты тревог"],
  ["Alert Live State","Стан тривог","Состояние тревог"],
  ["Alert Data Age","Вік даних тривог","Возраст данных тревог"],
  ["Alert Live Revision","Ревізія тривог","Ревизия тревог"],
  ["Alert Accepted Frames","Прийняті кадри","Принятые кадры"],
  ["Alert Rejected Frames","Відхилені кадри","Отклонённые кадры"],
  ["Setup Status","Стан налаштування","Состояние настройки"],
  ["Finish Setup","Завершити налаштування","Завершить настройку"],
  ["Repeat Setup","Повторити налаштування","Повторить настройку"],
  ["Reset Network","Скинути мережу","Сбросить сеть"],
  ["Reset Integrations","Скинути інтеграції","Сбросить интеграции"],
  ["Factory Reset Phrase","Фраза скидання","Фраза сброса"],
  ["Factory Reset Status","Стан скидання","Статус сброса"],
  ["Factory Reset","Повне скидання","Полный сброс"],
  ["Selected Region","Регіон","Регион"],
  ["Weather Location","Місто погоди","Город погоды"],
  ["Weather City Search","Пошук міста","Поиск города"],
  ["Weather Search Result 1","Результат 1","Результат 1"],
  ["Weather Search Result 2","Результат 2","Результат 2"],
  ["Weather Search Result 3","Результат 3","Результат 3"],
  ["Use Weather Result 1","Вибрати 1","Выбрать 1"],
  ["Use Weather Result 2","Вибрати 2","Выбрать 2"],
  ["Use Weather Result 3","Вибрати 3","Выбрать 3"],
  ["Refresh Weather","Оновити погоду","Обновить погоду"],
  ["SHT4x Status","Стан SHT41/SHT45","Состояние SHT41/SHT45"],
  ["Environment Temperature","Температура повітря","Температура воздуха"],
  ["Environment Humidity","Вологість повітря","Влажность воздуха"],
  ["Panel Sounds Enabled","Звуки панелі","Звуки панели"],
  ["Buzzer Test","Тест бузера","Тест буззера"],
  ["SvitloBot Enabled","SvitloBot увімкнено","SvitloBot включён"],
  ["SvitloBot Channel Key","Ключ каналу SvitloBot","Ключ канала SvitloBot"],
  ["SvitloBot Ping Now","Надіслати ping SvitloBot","Отправить ping SvitloBot"],
  ["SvitloBot Status","Статус SvitloBot","Статус SvitloBot"],
  ["SvitloBot Delivery Errors","Помилки доставки SvitloBot","Ошибки доставки SvitloBot"],
  ["SvitloBot Response Duration","Час відповіді SvitloBot","Время ответа SvitloBot"],
  ["SvitloBot Response Code","Код відповіді SvitloBot","Код ответа SvitloBot"],
  ["Network IPv4 Mode","Режим IPv4","Режим IPv4"],
  ["Network SSID","Wi-Fi SSID","Wi-Fi SSID"],
  ["Network Password","Пароль Wi-Fi","Пароль Wi-Fi"],
  ["Network Static IP","Статична IP-адреса","Статический IP"],
  ["Network Gateway","Шлюз","Шлюз"],
  ["Network Subnet","Маска підмережі","Маска подсети"],
  ["Network DNS 1","DNS 1","DNS 1"],
  ["Network DNS 2","DNS 2","DNS 2"],
  ["Network Profile Status","Стан мережевого профілю","Состояние сетевого профиля"],
  ["Apply Network Settings","Застосувати налаштування мережі","Применить настройки сети"],
  ["Starlink Enabled","Starlink увімкнено","Starlink включён"],
  [STARLINK_HOST_ENTITY,"Адреса Starlink","Адрес Starlink"],
  ["Starlink Port","Порт Starlink","Порт Starlink"],
  ["Starlink Status","Статус Starlink","Статус Starlink"],
  ["Starlink Latency","Затримка Starlink","Задержка Starlink"],
  ["Starlink Probe Errors","Помилки перевірки Starlink","Ошибки проверки Starlink"],
  ["Starlink GPS Inhibit","Блокувати GPS Starlink","Блокировать GPS Starlink"],
  ["Starlink GPS Status","Стан GPS Starlink","Состояние GPS Starlink"],
  ["Starlink GPS Satellites","Супутники GPS Starlink","Спутники GPS Starlink"],
  ["Starlink gRPC Status","gRPC статус Starlink","gRPC статус Starlink"],
  ["Starlink Endpoint","Endpoint Starlink","Endpoint Starlink"],
  ["Starlink Probe Now","Перевірити Starlink зараз","Проверить Starlink сейчас"],
  ["Shelly Enabled","Shelly увімкнено","Shelly включён"],
  ["Shelly Host","Адреса Shelly","Адрес Shelly"],
  ["Shelly Port","Порт Shelly","Порт Shelly"],
  ["Shelly Password","Пароль Shelly","Пароль Shelly"],
  ["Shelly Mains Channel","Канал мережі Shelly","Канал сети Shelly"],
  ["Shelly Status","Статус Shelly","Статус Shelly"],
  ["Shelly Endpoint","Endpoint Shelly","Endpoint Shelly"],
  ["Shelly HTTP Status","HTTP статус Shelly","HTTP статус Shelly"],
  ["Shelly Refresh Now","Оновити Shelly зараз","Обновить Shelly сейчас"],
  ["Shelly CH0 Voltage","Shelly CH0 напруга","Shelly CH0 напряжение"],
  ["Shelly CH0 Current","Shelly CH0 струм","Shelly CH0 ток"],
  ["Shelly CH0 Active Power","Shelly CH0 активна потужність","Shelly CH0 активная мощность"],
  ["Shelly CH0 Apparent Power","Shelly CH0 повна потужність","Shelly CH0 полная мощность"],
  ["Shelly CH0 Power Factor","Shelly CH0 коефіцієнт потужності","Shelly CH0 коэффициент мощности"],
  ["Shelly CH0 Frequency","Shelly CH0 частота","Shelly CH0 частота"],
  ["Shelly CH1 Voltage","Shelly CH1 напруга","Shelly CH1 напряжение"],
  ["Shelly CH1 Current","Shelly CH1 струм","Shelly CH1 ток"],
  ["Shelly CH1 Active Power","Shelly CH1 активна потужність","Shelly CH1 активная мощность"],
  ["Shelly CH1 Apparent Power","Shelly CH1 повна потужність","Shelly CH1 полная мощность"],
  ["Shelly CH1 Power Factor","Shelly CH1 коефіцієнт потужності","Shelly CH1 коэффициент мощности"],
  ["Shelly CH1 Frequency","Shelly CH1 частота","Shelly CH1 частота"]
]){LABELS.uk[k]=u;LABELS.ru[k]=r;}

function node(tag, cls, text) {
  const el = document.createElement(tag);
  if (cls) el.className = cls;
  if (text != null) el.textContent = text;
  return el;
}
function safeSsidDisplay(value) {
  return String(value ?? "").replace(/[\u061C\u200E\u200F\u202A-\u202E\u2066-\u2069]/g, "�");
}
function localeFromLanguage(value) {
  if (value === "Українська") return "uk";
  if (value === "Русский") return "ru";
  return "en";
}
function label(locale, name) {
  const alarm=/^Alarm ([123]) (Time|Enabled|Repeat)$/.exec(name);
  if(alarm && locale !== "en") {
    const fields=locale==="ru"?["Будильник","Время","Включение","Повтор"]:["Будильник","Час","Увімкнення","Повтор"];
    return fields[0]+" "+alarm[1]+" · "+fields[["Time","Enabled","Repeat"].indexOf(alarm[2])+1];
  }
  return LABELS[locale]?.[name] || name;
}
function apiPath(entity, action, params = {}) {
  const q = new URLSearchParams(params).toString();
  return `/${entity.domain}/${encodeURIComponent(entity.name)}/${action}${q ? `?${q}` : ""}`;
}
const STATUS_LOCAL = {
  uk:["Актуально","Застаріло","Немає даних","Не підключено","Помилка"],
  ru:["Актуально","Устарело","Нет данных","Не подключено","Ошибка"],
};
function statusText(locale, value) {
  const raw=String(value ?? "—"), upper=raw.toUpperCase();
  const securityKey={FACTORY:"factoryCredentials",CUSTOM:"customCredentials",DEFAULT:"defaultPassword"}[upper];
  if (securityKey) return (STRINGS[locale] || STRINGS.en)[securityKey];
  const i=["LIVE","STALE","NO DATA","NOT DETECTED","ERROR"].indexOf(upper);
  return i < 0 ? raw : STATUS_LOCAL[locale]?.[i] || raw;
}
function badgeClass(value) {
  const v = String(value || "").toUpperCase();
  if (/^(LIVE|CUSTOM|OK|ON|CONNECTED|COMPLETE)$/.test(v)) return "good";
  if (/FACTORY|STALE|FALLBACK|INCOMPLETE/.test(v)) return "warn";
  if (/DEFAULT|ERROR|FAIL|OFFLINE|DISCONNECTED/.test(v)) return "bad";
  if (/^NO |NOT DETECTED|NONE/.test(v)) return "quiet";
  return "";
}

class UspApp extends HTMLElement {
  constructor() {
    super();
    this.__uspPrivate_entities = new Map();
    this.__uspPrivate_groups = new Map();
    this.__uspPrivate_pendingButtons = new Set();
    this.__uspPrivate_locale = "en";
    this.__uspPrivate_activeTab = tabPreference() || "overview";
    if (!TAB_ORDER.includes(this.__uspPrivate_activeTab)) this.__uspPrivate_activeTab = "overview";
    this.__uspPrivate_renderQueued = false;
    this.__uspPrivate_rendered = false;
    this.__uspPrivate_focusTab = "";
    this.__uspSpotify = {state:"unknown", session_storage:"unknown", exchange_available:false,
      loaded:false, loading:false, error:"", updated_at:0};
    this.__uspPrivate_entityCards = new Map();
    this.__uspTextDrafts = new Map();
    this.__uspTextControls = new Map();
    this.__uspStreetRequest = 0;
    this.__uspHouseRequest = 0;
    this.__uspGroupRequest = 0;
    this.__uspPrivate_kyiv = {street:"", streetId:null, house:"", result:null, streets:[], houses:[], busy:false, message:""};
    this.__uspAddressLookupAllowed = false;
    this.__uspPrivate_streetTimer = null;
    this.__uspPrivate_houseTimer = null;
    this.__uspPrivate_root = node("main", "usp-shell");
    this.append(this.__uspPrivate_root);
    this.connect();
    window.addEventListener("resize", () => this.revealTab());
  }
  connect() {
    this.__uspPrivate_events = new EventSource("/events");
    this.__uspPrivate_events.addEventListener("ping", e => {
      try { this.__uspPrivate_meta = JSON.parse(e.data); } catch (_) {}
      this.sc(true);
      if (this.__uspPrivate_activeTab === "spotify") this.__uspLoadSpotifyStatus(false);
      if (!this.__uspPrivate_rendered) this.queue();
      else this.uf();
    });
    this.__uspPrivate_events.addEventListener("sorting_group", e => {
      try {
        const g = JSON.parse(e.data);
        const previous = this.__uspPrivate_groups.get(g.name);
        this.__uspPrivate_groups.set(g.name, g);
        if (!this.__uspPrivate_rendered || !previous) this.queue();
      } catch (_) {}
    });
    this.__uspPrivate_events.addEventListener("state", e => {
      try {
        const value = JSON.parse(e.data);
        const previous = this.__uspPrivate_entities.get(value.id);
        const previousLocale = this.__uspPrivate_locale;
        this.__uspPrivate_entities.set(value.id, {...(previous || {}), ...value});
        if (value.name === UI_LANGUAGE_ENTITY) this.__uspPrivate_locale = localeFromLanguage(value.value ?? value.state);
        if (value.name === "Setup Status" && String(value.state ?? value.value) !== "COMPLETE" && !tabPreference()) this.__uspPrivate_activeTab = "setup";
        if (!this.__uspPrivate_rendered || !previous || previousLocale !== this.__uspPrivate_locale || (this.__uspPrivate_activeTab === "setup" && document.activeElement === document.body)) this.queue();
        else if (this.__uspPrivate_activeTab !== "setup") this.patch(value.id);
      } catch (_) {}
    });
    this.__uspPrivate_events.onerror = () => this.sc(false);
  }
  sc(online) {
    this.online = online;
    if (this.connectionText) {
      const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
      this.connectionText.textContent = online ? t.connected : t.reconnecting;
      this.connectionDot.className = `usp-dot ${online ? "online" : "offline"}`;
    }
  }
  queue() {
    if (this.__uspPrivate_renderQueued) return;
    this.__uspPrivate_renderQueued = true;
    requestAnimationFrame(() => { this.__uspPrivate_renderQueued = false; this.render(); });
  }
  async post(entity, action, params = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch(apiPath(entity, action, params), {method:"POST", body:"", cache:"no-store", signal:controller.signal});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      this.er("");
      return true;
    } catch (error) {
      this.er(`Web API: ${error.message}`);
      return false;
    } finally { clearTimeout(timer); }
  }
  async gj(path) {
    if (!this.__uspAddressLookupAllowed) throw new Error("External address lookup consent required");
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 10000);
    try {
      const response = await fetch(`${KYIV_GROUP_API}${path}`, {cache:"no-store", signal:controller.signal});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      return await response.json();
    } finally { clearTimeout(timer); }
  }
  er(message) {
    if (!this.__uspPrivate_errorBox) return;
    this.__uspPrivate_errorBox.textContent = message;
    this.__uspPrivate_errorBox.classList.toggle("show", !!message);
  }
  setTab(tab, focusTab = false) {
    if (!TAB_ORDER.includes(tab)) return;
    if (this.__uspPrivate_activeTab === "svitlo" && tab !== "svitlo") this.__uspInvalidateAddress();
    this.__uspPrivate_activeTab = tab;
    this.__uspPrivate_focusTab = focusTab ? tab : "";
    tabPreference(tab);
    this.queue();
    if (tab === "spotify") this.__uspLoadSpotifyStatus(true);
  }
  __uspTabKey(event, tab) {
    if (!["ArrowLeft","ArrowRight","Home","End"].includes(event.key)) return;
    event.preventDefault();
    let index = TAB_ORDER.indexOf(tab);
    if (event.key === "Home") index = 0;
    else if (event.key === "End") index = TAB_ORDER.length - 1;
    else index = (index + (event.key === "ArrowRight" ? 1 : -1) + TAB_ORDER.length) % TAB_ORDER.length;
    this.setTab(TAB_ORDER[index], true);
  }
  uf() {
    if (!this.__uspPrivate_footerEl) return;
    this.__uspPrivate_footerEl.textContent = `Ukraine Status Panel · ${this.__uspPrivate_meta?.uptime ? `uptime ${this.__uspPrivate_meta.uptime}s` : "ESPHome"}`;
  }
  patch(id) {
    const current = this.__uspPrivate_entityCards.get(id);
    const entity = this.__uspPrivate_entities.get(id);
    if (!entity || !current?.isConnected) return;
    const focus=current.querySelector(":focus");
    if(focus&&entity.domain!="switch")return;
    const replacement=this.card(entity);
    current.replaceWith(replacement);
    if(focus)replacement.lastChild.lastChild.focus();
  }
  render() {
    const focused = this.__uspPrivate_focusTab ? null : this.__uspCaptureFocus();
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
    this.__uspPrivate_entityCards.clear();
    this.__uspPrivate_root.replaceChildren();
    this.__uspRenderHeader(t);
    this.__uspPrivate_errorBox=node("div","usp-error");this.__uspPrivate_errorBox.role="alert";this.__uspPrivate_root.append(this.__uspPrivate_errorBox);
    const content = node("div");
    content.id = "usp-tabpanel";
    content.setAttribute("role", "tabpanel");
    content.setAttribute("aria-labelledby", `usp-tab-${this.__uspPrivate_activeTab}`);
    const intro=TAB_INTRO[this.__uspPrivate_locale]?.[this.__uspPrivate_activeTab]; intro&&content.append(node("div","usp-note",intro));
    if (this.__uspPrivate_activeTab === "spotify") content.append(this.spotifyPanel(t));
    if (this.__uspPrivate_activeTab === "setup") content.append(this.setupPanel(t));
    if (this.__uspPrivate_activeTab === "maintenance") content.append(this.maintenancePanel(t));
    if (this.__uspPrivate_activeTab === "svitlo") content.append(this.kyivFinderPanel(t));
    const grouped = new Map(GROUPS.map(([name]) => [name, []]));
    for (const entity of this.__uspPrivate_entities.values()) {
      if (grouped.has(entity.sorting_group)) grouped.get(entity.sorting_group).push(entity);
    }
    for (const [name, tab] of GROUPS) {
      if (tab !== this.__uspPrivate_activeTab) continue;
      const entities = grouped.get(name).sort((a,b) => (a.sorting_weight ?? 50) - (b.sorting_weight ?? 50));
      if (!entities.length) continue;
      const section = node("section", "usp-group");
      section.append(node("h2", "", t.groups[name] || name));
      const grid = node("div", "usp-grid");
      for (const entity of entities) grid.append(this.card(entity));
      section.append(grid); content.append(section);
    }
    this.__uspPrivate_root.append(content);
    this.__uspPrivate_footerEl = node("div", "usp-footer");
    this.uf();
    this.__uspPrivate_root.append(this.__uspPrivate_footerEl);
    this.__uspPrivate_rendered = true;
    this.__uspRestoreFocus(focused);
  }
  /** Render the stable navigation labels separately from page content. */
  __uspRenderHeader(t) {
    document.documentElement.lang = this.__uspPrivate_locale;
    const head = node("header", "usp-head");
    const titleWrap = node("div");
    titleWrap.append(node("h1", "usp-title", this.__uspPrivate_meta?.title || "Ukraine Status Panel"));
    titleWrap.append(node("div", "usp-subtitle", t.subtitle));
    head.append(titleWrap);
    const conn = node("div", "usp-connection");
    this.connectionDot = node("span", `usp-dot ${this.online ? "online" : "offline"}`);
    this.connectionText = node("span", "", this.online ? t.connected : t.reconnecting);
    conn.append(this.connectionDot, this.connectionText); head.append(conn); this.__uspPrivate_root.append(head);

    const nav = node("nav", "usp-tabs");
    nav.setAttribute("role", "tablist");
    nav.setAttribute("aria-label", t.subtitle);
    let focusTarget = null;
    for (const tab of TAB_ORDER) {
      const active = tab === this.__uspPrivate_activeTab;
      const button = node("button", `usp-tab${active ? " active" : ""}`, t.tabs[tab] || tab);
      button.id = `usp-tab-${tab}`;
      button.dataset.focusKey = `tab-${tab}`;
      button.setAttribute("role", "tab");
      button.setAttribute("aria-selected", active ? "true" : "false");
      button.setAttribute("aria-controls", "usp-tabpanel");
      button.tabIndex = active ? 0 : -1;
      button.onclick = () => this.setTab(tab);
      button.onkeydown = event => this.__uspTabKey(event, tab);
      if (this.__uspPrivate_focusTab === tab) focusTarget = button;
      nav.append(button);
    }
    this.__uspPrivate_root.append(this.__uspPrivate_tabs = nav);
    if (focusTarget) {
      focusTarget.focus({preventScroll:true});
      this.__uspPrivate_focusTab = "";
    }
    this.revealTab();
  }
  /** Keep the selected tab visible; scrolling stays inside its navigation row. */
  revealTab() {
    const nav = this.__uspPrivate_tabs;
    const active = nav?.querySelector(".active");
    if (active) nav.scrollLeft = active.offsetLeft - nav.offsetLeft - 4;
  }
  /** Keep the current input and selection across a required full render. */
  __uspCaptureFocus() {
    const input = document.activeElement;
    if (!input || !this.__uspPrivate_root.contains(input) || !input.dataset.focusKey) return null;
    return {key:input.dataset.focusKey, value:input.value,
      start:input.selectionStart, end:input.selectionEnd};
  }
  __uspRestoreFocus(saved) {
    if (!saved) return;
    const input = [...this.__uspPrivate_root.querySelectorAll("[data-focus-key]")].find(el => el.dataset.focusKey === saved.key);
    if (!input) return;
    if (saved.value != null) input.value = saved.value;
    input.focus({preventScroll:true});
    if (saved.start != null) {
      try { input.setSelectionRange(saved.start, saved.end); } catch (_) {}
    }
  }
  /** Store drafts only in memory; never write secret values to storage. */
  __uspTextControl(entity, t) {
    if (!this.__uspTextDrafts.has(entity.id)) this.__uspTextDrafts.set(entity.id, {value:"", pending:false, message:""});
    const draft = this.__uspTextDrafts.get(entity.id);
    const wrap = node("div");
    const actions = node("div", "usp-actions");
    const input = node("input");
    input.type = entity.mode === 1 ? "password" : "text";
    input.placeholder = entity.mode === 1 ? "••••••••" : String(entity.state ?? entity.value ?? "");
    if (/^Alarm [123] Time$/.test(entity.name)) input.pattern="([01][0-9]|2[0-3]):[0-5][0-9]";
    input.minLength = Number(entity.min_length || 0);
    input.maxLength = Number(entity.max_length || 255);
    const allowEmpty = input.minLength === 0;
    input.required = !allowEmpty;
    input.value = draft.value;
    input.oninput = () => { draft.value = input.value; draft.message = ""; status.textContent = ""; };
    const save = node("button", "", draft.pending ? t.saving : t.save);
    save.disabled = draft.pending;
    save.onclick = () => input.reportValidity() && this.__uspSaveText(entity);
    const status = node("div", "usp-muted", draft.message);
    status.role = "status";
    this.__uspTextControls.set(entity.id, {input, save, status});
    actions.append(input, save); wrap.append(actions, status);
    return wrap;
  }
  async __uspSaveText(entity) {
    const draft = this.__uspTextDrafts.get(entity.id);
    if (!draft || draft.pending) return;
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
    const submitted = draft.value;
    draft.pending = true;
    draft.message = t.saving;
    this.__uspUpdateTextControl(entity.id);
    const ok = await this.post(entity, "set", {value:submitted});
    draft.pending = false;
    // A later edit belongs to the user and must survive an older response.
    if (ok && draft.value === submitted) draft.value = "";
    draft.message = ok ? t.sent : t.saveError;
    this.__uspUpdateTextControl(entity.id);
  }
  __uspUpdateTextControl(id) {
    const controls = this.__uspTextControls.get(id), draft = this.__uspTextDrafts.get(id);
    if (!controls || !draft || !controls.input.isConnected) return;
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
    controls.input.value = draft.value;
    controls.save.disabled = draft.pending;
    controls.save.textContent = draft.pending ? t.saving : t.save;
    controls.status.textContent = draft.message;
  }
  en(n,d) { return [...this.__uspPrivate_entities.values()].find(e=>e.name===n&&(!d||e.domain===d)); }
  safeBackup() {
    const value=(n,d)=>{const e=this.en(n,d); return e?.state??e?.value??null};
    const doc={schema:"usp-safe-config-v1",exportedAt:new Date().toISOString(),firmware:value("Firmware Version"),timezone:"Europe/Kyiv",settings:{language:value(UI_LANGUAGE_ENTITY),alertPalette:value("Alert Palette"),region:value("Selected Region","select"),weather:value("Weather Location"),outageGroup:value(OUTAGE_GROUP_ENTITY),sounds:value("Panel Sounds Enabled"),network:{mode:value("Network IPv4 Mode"),staticIp:value("Network Static IP"),gateway:value("Network Gateway"),subnet:value("Network Subnet"),dns1:value("Network DNS 1"),dns2:value("Network DNS 2")},starlink:{enabled:value("Starlink Enabled"),host:value(STARLINK_HOST_ENTITY),port:value("Starlink Port"),gpsInhibit:value("Starlink GPS Inhibit")},shelly:{enabled:value("Shelly Enabled"),host:value("Shelly Host"),port:value("Shelly Port"),channel:value("Shelly Mains Channel")},svitlobot:{enabled:value("SvitloBot Enabled")}},redacted:["Alarm schedules","Wi-Fi credentials","Web/API/OTA credentials","SvitloBot secret","Shelly password"]};
    const blob=new Blob([JSON.stringify(doc,null,2)],{type:"application/json"}), url=URL.createObjectURL(blob), a=document.createElement("a");
    a.href=url; a.download=`ukraine-status-panel-safe-${Date.now()}.json`; a.click(); setTimeout(()=>URL.revokeObjectURL(url),0);
  }
  ipv4(v) { const a=String(v).split("."); return v===""||(a.length==4&&a.every(x=>/^\d{1,3}$/.test(x)&&+x<256)); }
  restorePlan(d) {
    if(!d||d.schema!=="usp-safe-config-v1"||!d.settings||typeof d.settings!=="object") return null;
    const s={...d.settings},n=s.network||{},st=s.starlink||{},sh=s.shelly||{},sv=s.svitlobot||{};
    if (s.outageGroup === "--") s.outageGroup = null;
    const p=[[UI_LANGUAGE_ENTITY,"select",s.language],["Alert Palette","select",s.alertPalette],["Selected Region","select",s.region],[OUTAGE_GROUP_ENTITY,"text",s.outageGroup],["Panel Sounds Enabled","switch",s.sounds],["Network IPv4 Mode","select",n.mode],["Network Static IP","text",n.staticIp],["Network Gateway","text",n.gateway],["Network Subnet","text",n.subnet],["Network DNS 1","text",n.dns1],["Network DNS 2","text",n.dns2],["Starlink Enabled","switch",st.enabled],[STARLINK_HOST_ENTITY,"text",st.host],["Starlink Port","number",st.port],["Shelly Enabled","switch",sh.enabled],["Shelly Host","text",sh.host],["Shelly Port","number",sh.port],["Shelly Mains Channel","select",sh.channel],["SvitloBot Enabled","switch",sv.enabled]].filter(x=>x[2]!=null);
    for(const x of p){const e=this.en(x[0],x[1]),v=x[2]; if(!e)return null; if(x[1]=="select"&&!(e.option||[]).includes(String(v)))return null; if(x[1]==="switch"&&![true,false,"ON","OFF"].includes(v))return null; if(x[1]==="number"&&(!Number.isInteger(+v)||+v<+e.min_value||+v>+e.max_value))return null; if(x[1]==="text"){const z=String(v); if(z.length<+(e.min_length||0)||z.length>+(e.max_length||255))return null; if(x[0].startsWith("Network ")&&!this.ipv4(z))return null; if(x[0]===STARLINK_HOST_ENTITY&&(/\s/.test(z)||!z))return null;}}
    const g=String(s.outageGroup??"").split("."); if(s.outageGroup!=null&&(!/^\d{1,2}(\.[1-9])?$/.test(String(s.outageGroup))||+g[0]>60))return null; return p;
  }
  async loadRestore(f,t) { this.__uspPrivate_restore=null; if(!f||f.size>16384){this.__uspPrivate_restoreMsg=t.restoreBad;this.queue();return} try{const p=this.restorePlan(JSON.parse(await f.text())); if(!p)throw 0; this.__uspPrivate_restore=p;this.__uspPrivate_restoreMsg=`${t.restoreReady}: ${p.length} · skip weather/GPS`; }catch(_){this.__uspPrivate_restoreMsg=t.restoreBad} this.queue(); }
  async applyRestore(t) { const p=this.__uspPrivate_restore; if(!p)return; let ok=0; for(const [n,d,v] of p){const e=this.en(n,d); let a="set",q=d==="select"?{option:String(v)}:{value:String(v)}; if(d==="switch"){a=(v===true||v==="ON")?"turn_on":"turn_off";q={}} if(await this.post(e,a,q))ok++;} this.__uspPrivate_restoreMsg=`${t.restoreDone}: ${ok}/${p.length} · network staged`; this.queue(); }
  wifiControl(e,t,c) {
    const r=node("div","usp-actions"),b=node("button","usp-primary",t.scanWifi),s=node("select"),h=node("button","",t.hiddenWifi);
    b.onclick=async()=>{b.disabled=1;b.textContent=t.scanWifi+"…";try{let q=await fetch("/usp/network/scan",{method:"POST"}),d=await q.json();if(!q.ok&&q.status!==202)throw Error(d.error);for(let i=0;i<60&&q.status===202;i++){await new Promise(a=>setTimeout(a,250));q=await fetch("/usp/network/scan");d=await q.json()}if(!q.ok||d.state!=="ready")throw Error(d.error||"timeout");s.replaceChildren(node("option","","—"));for(const n of d.networks){const o=node("option","",`${safeSsidDisplay(n.ssid)} · ${n.rssi}dBm${n.secure?" WPA":""}`);o.value=n.ssid;s.append(o)}s.disabled=s.length<2;this.er("")}catch(x){this.er(x)}finally{b.disabled=0;b.textContent=t.scanWifi}};
    s.onchange=()=>s.value&&this.post(e,"set",{value:s.value});h.onclick=()=>{const x=prompt(t.hiddenWifi);x&&this.post(e,"set",{value:x.trim()})};r.append(b,s,h);c.append(r);
  }
  /** Read only the panel-local authenticated Spotify status; provider URLs never run here. */
  async __uspLoadSpotifyStatus(force = false) {
    const current=this.__uspSpotify, now=Date.now();
    if (current.loading || (!force && current.loaded && now-current.updated_at < 3000)) return;
    current.loading=true;current.error="";this.queue();
    const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);
    try {
      const response=await fetch("/usp/spotify/auth/status",{credentials:"same-origin",cache:"no-store",signal:controller.signal});
      const text=await response.text();
      if (text.length>8192) throw new Error("oversize");
      const data=JSON.parse(text), states=new Set(["idle","awaiting_user","code_pending","exchanging","connected","reauth"]),
        storage=new Set(["hardware","local","ready","unprovisioned","blocked","error"]);
      if(!response.ok || data?.ok!==true || !states.has(data.state) || !storage.has(data.session_storage)) throw new Error("invalid");
      this.__uspSpotify={state:data.state,session_storage:data.session_storage,
        exchange_available:data.exchange_available===true,loaded:true,loading:false,error:"",updated_at:Date.now()};
    } catch (_) {
      this.__uspSpotify={state:"unknown",session_storage:"unknown",exchange_available:false,
        loaded:true,loading:false,error:"status",updated_at:Date.now()};
    } finally { clearTimeout(timer); this.queue(); }
  }
  __uspSpotifyStateText(t,state) {
    if (state==="connected") return t.spotifyConnected;
    if (state==="reauth") return t.spotifyReauth;
    if (["awaiting_user","code_pending","exchanging"].includes(state)) return t.spotifyPending;
    if (state==="idle") return t.spotifyIdle;
    return state==="unknown" ? t.spotifyUnknown : t.spotifyError;
  }
  __uspSpotifyStorageText(t,state) {
    if (state==="hardware") return t.spotifyStorageHardware;
    if (state==="local") return t.spotifyStorageLocal;
    if (state==="ready") return t.spotifyStorageReady;
    if (state==="unprovisioned") return t.spotifyStorageUnprovisioned;
    if (state==="blocked") return t.spotifyStorageBlocked;
    return state==="unknown" ? t.spotifyUnknown : t.spotifyStorageError;
  }
  /** Dedicated Stage5 configuration/status surface; secrets are never copied into the DOM. */
  spotifyPanel(t) {
    const section=node("section","usp-group"),card=node("article","usp-card control"),status=this.__uspSpotify;
    const state=node("span",`usp-pill ${status.state==="connected"?"good":status.state==="reauth"?"bad":status.state==="idle"?"quiet":"warn"}`,status.error?t.spotifyError:this.__uspSpotifyStateText(t,status.state));
    state.dataset.spotifyState=status.state;
    const storage=node("span",`usp-pill ${["hardware","local","ready"].includes(status.session_storage)?"good":status.session_storage==="unprovisioned"?"warn":status.session_storage==="unknown"?"quiet":"bad"}`,status.error?t.spotifyError:this.__uspSpotifyStorageText(t,status.session_storage));
    storage.dataset.spotifyStorage=status.session_storage;
    const connection=node("div","usp-row");connection.append(node("span","usp-name",t.spotifyConnection),state);
    const storageRow=node("div","usp-row");storageRow.append(node("span","usp-name",t.spotifyStorage),storage);
    const actions=node("div","usp-actions"),manage=node("button","usp-primary",t.spotifyManage),refresh=node("button","",t.spotifyRefresh);
    manage.dataset.focusKey="spotify-auth";manage.onclick=()=>{location.href="/usp/spotify/auth/";};
    refresh.dataset.focusKey="spotify-refresh";refresh.disabled=status.loading;refresh.onclick=()=>this.__uspLoadSpotifyStatus(true);
    actions.append(manage,refresh);
    card.append(node("div","usp-muted",t.spotifyHint),connection,storageRow,actions);
    section.append(node("h2","",t.spotifySetup),card);
    return section;
  }
  maintenancePanel(t) {
    const sec=node("section","usp-group"),c=node("article","usp-card control"),ex=node("button","usp-primary",t.exportSafe),pick=node("button","",t.importSafe),f=node("input"),ap=node("button","",t.restoreSafe); ex.onclick=()=>this.safeBackup(); f.type="file";f.accept=".json,application/json";f.hidden=true;pick.onclick=()=>f.click();f.onchange=()=>this.loadRestore(f.files?.[0],t);ap.disabled=!this.__uspPrivate_restore;ap.onclick=()=>this.applyRestore(t);c.append(node("div","usp-muted",t.backupNote),ex,pick,f,ap);if(this.__uspPrivate_restoreMsg)c.append(node("div","usp-muted",this.__uspPrivate_restoreMsg));sec.append(node("h2","",t.backupTitle),c);return sec;
  }
  setupPanel(t) {
    const sec=node("section","usp-group"), grid=node("div","usp-grid");
    const get=n=>this.en(n);
    const rows=[["Wi-Fi SSID","network"],[UI_LANGUAGE_ENTITY,"overview"],[null,null,"Europe/Kyiv"],["Selected Region","location"],["Weather Location","location"],[OUTAGE_GROUP_ENTITY,"svitlo"]];
    rows.forEach((r,i)=>{ const e=r[0]&&get(r[0]), v=r[2]??e?.state??e?.value??"—", shown=r[0]==="Wi-Fi SSID"?safeSsidDisplay(v):v, c=node("article","usp-card"); c.append(node("div","usp-name",`${i+1}. ${t.setup[i]}`),node("div","usp-value",shown)); if(r[1]){const b=node("button","",t.open); b.onclick=()=>this.setTab(r[1]); c.append(b)} grid.append(c) });
    sec.append(node("h2","",t.groups.Setup),grid); return sec;
  }
  /** Build address fields once and update their dependent actions in place. */
  kyivFinderPanel(t) {
    const section = node("section", "usp-group usp-finder");
    section.append(node("h2", "", t.finderTitle));
    const card = node("article", "usp-card usp-finder-card");
    if (!this.__uspAddressLookupAllowed) {
      const enable = node("button", "usp-primary usp-external-consent", t.enableExternalLookup);
      enable.onclick = () => { this.__uspAddressLookupAllowed = true; this.queue(); };
      card.append(node("div", "usp-muted", t.externalPrivacy), enable, node("div", "usp-muted", t.externalSource));
      section.append(card);
      return section;
    }
    const fields = node("div", "usp-finder-fields");
    fields.append(this.__uspCreateAddressField("street", t), this.__uspCreateAddressField("house", t));
    const actions = node("div", "usp-actions");
    this.__uspKyivFindButton = node("button", "", t.findGroup);
    this.__uspKyivFindButton.onclick = () => this.lookupGroup();
    this.__uspKyivResultBadge = node("span", "usp-pill good");
    this.__uspKyivApplyButton = node("button", "usp-primary", t.ag);
    this.__uspKyivApplyButton.onclick = () => this.applyGroup();
    actions.append(this.__uspKyivFindButton, this.__uspKyivResultBadge, this.__uspKyivApplyButton);
    this.__uspKyivMessage = node("div", "usp-muted");
    this.__uspKyivMessage.role = "status";
    card.append(fields, actions, this.__uspKyivMessage, node("div", "usp-muted", t.externalSource));
    section.append(card); this.__uspKyivSection = section;
    this.__uspUpdateAddressActions();
    return section;
  }
  __uspCreateAddressField(kind, t) {
    const streetMode = kind === "street";
    const wrap = node("label", "usp-field");
    wrap.append(node("span", "usp-name", streetMode ? t.street : t.house));
    const input = node("input");
    input.type = "text"; input.value = this.__uspPrivate_kyiv[kind];
    input.dataset.focusKey = `kyiv-${kind}`;
    input.placeholder = streetMode ? t.streetHint : t.selectStreet;
    input.disabled = !streetMode && this.__uspPrivate_kyiv.streetId == null;
    const list = node("div", "usp-suggestions");
    if (streetMode) { this.__uspKyivStreetInput = input; this.__uspKyivStreetList = list; }
    else { this.__uspKyivHouseInput = input; this.__uspKyivHouseList = list; }
    input.oninput = () => this.__uspAddressInputChanged(kind, input.value);
    if (!streetMode) input.onfocus = () => {
      if (this.__uspPrivate_kyiv.streetId != null && !this.__uspPrivate_kyiv.houses.length) this.lh(input.value.trim());
    };
    this.rs(list, streetMode ? this.__uspPrivate_kyiv.streets : this.__uspPrivate_kyiv.houses,
      item => streetMode ? this.cs(item) : this.ch(item));
    wrap.append(input, list);
    return wrap;
  }
  __uspAddressKey() {
    return JSON.stringify([this.__uspPrivate_kyiv.streetId, this.__uspPrivate_kyiv.street.trim(), this.__uspPrivate_kyiv.house.trim()]);
  }
  /** Invalidate old results before waiting for any new request. */
  __uspInvalidateAddress() {
    clearTimeout(this.__uspPrivate_streetTimer); clearTimeout(this.__uspPrivate_houseTimer);
    this.__uspStreetRequest++; this.__uspHouseRequest++; this.__uspGroupRequest++;
    this.__uspPrivate_kyiv.result = null; this.__uspPrivate_kyiv.groupAddress = null;
    this.__uspPrivate_kyiv.busy = false; this.__uspPrivate_kyiv.message = "";
  }
  __uspAddressInputChanged(kind, value) {
    this.__uspInvalidateAddress();
    this.__uspPrivate_kyiv[kind] = value;
    if (kind === "street") {
      this.__uspPrivate_kyiv.streetId = null;
      this.__uspPrivate_kyiv.house = ""; this.__uspPrivate_kyiv.houses = []; this.__uspPrivate_kyiv.streets = [];
      this.__uspKyivHouseInput.value = ""; this.__uspKyivHouseInput.disabled = true;
      this.us("street"); this.us("house");
      if (value.trim().length >= 3) this.__uspPrivate_streetTimer = setTimeout(() => this.ls(value.trim()), 250);
    } else {
      this.__uspPrivate_kyiv.houses = []; this.us("house");
      if (this.__uspPrivate_kyiv.streetId != null) this.__uspPrivate_houseTimer = setTimeout(() => this.lh(value.trim()), 180);
    }
    this.__uspUpdateAddressActions();
  }
  __uspUpdateAddressActions() {
    if (!this.__uspKyivFindButton) return;
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
    const ready = this.__uspPrivate_kyiv.streetId != null && this.__uspPrivate_kyiv.house.trim().length > 0;
    const result = this.__uspPrivate_kyiv.result && this.__uspPrivate_kyiv.groupAddress === this.__uspAddressKey();
    this.__uspKyivFindButton.disabled = !ready || this.__uspPrivate_kyiv.busy || this.__uspPrivate_kyiv.applying;
    this.__uspKyivFindButton.textContent = this.__uspPrivate_kyiv.busy ? t.finding : t.findGroup;
    this.__uspKyivApplyButton.hidden = !result;
    this.__uspKyivApplyButton.disabled = !result || this.__uspPrivate_kyiv.busy || this.__uspPrivate_kyiv.applying;
    this.__uspKyivResultBadge.hidden = !result;
    this.__uspKyivResultBadge.textContent = result ? `${t.groupFound}: ${this.__uspGroupCode()}` : "";
    this.__uspKyivMessage.textContent = this.__uspPrivate_kyiv.message;
  }
  __uspGroupCode() {
    const result = this.__uspPrivate_kyiv.result;
    return result ? `${result.group}.${result.subgroup || 1}` : "";
  }
  cs(item) {
    this.__uspInvalidateAddress();
    this.__uspPrivate_kyiv.street = item.name; this.__uspPrivate_kyiv.streetId = item.id;
    this.__uspPrivate_kyiv.house = ""; this.__uspPrivate_kyiv.houses = []; this.__uspPrivate_kyiv.streets = [];
    this.__uspKyivStreetInput.value = item.name; this.us("street");
    this.__uspKyivHouseInput.disabled = false; this.__uspKyivHouseInput.value = "";
    this.__uspKyivHouseInput.placeholder = "13 / 38А"; this.us("house");
    this.__uspUpdateAddressActions(); this.lh("");
  }
  ch(item) {
    this.__uspInvalidateAddress();
    this.__uspPrivate_kyiv.house = item.name; this.__uspPrivate_kyiv.houses = [];
    this.__uspKyivHouseInput.value = item.name;
    this.us("house"); this.__uspUpdateAddressActions();
  }
  us(kind) {
    const streetMode = kind === "street";
    const container = streetMode ? this.__uspKyivStreetList : this.__uspKyivHouseList;
    if (!container || !container.isConnected) return;
    container.replaceChildren();
    this.rs(container, streetMode ? this.__uspPrivate_kyiv.streets : this.__uspPrivate_kyiv.houses,
      item => streetMode ? this.cs(item) : this.ch(item));
  }
  rs(container, items, choose) {
    for (const item of items.slice(0, 20)) {
      const button = node("button", "usp-suggestion", item.name);
      button.onclick = () => choose(item); container.append(button);
    }
  }
  async ls(query) {
    const request = ++this.__uspStreetRequest;
    let items = [];
    try { const data = await this.gj(`/api/streets?q=${encodeURIComponent(query)}`); items = Array.isArray(data) ? data : []; }
    catch (_) {}
    if (request !== this.__uspStreetRequest || query !== this.__uspPrivate_kyiv.street.trim()) return;
    this.__uspPrivate_kyiv.streets = items; this.us("street");
  }
  async lh(query) {
    const streetId = this.__uspPrivate_kyiv.streetId;
    if (streetId == null) return;
    const request = ++this.__uspHouseRequest;
    let items = [];
    try { const data = await this.gj(`/api/houses?streetId=${encodeURIComponent(streetId)}&q=${encodeURIComponent(query)}`); items = Array.isArray(data) ? data : []; }
    catch (_) {}
    if (request !== this.__uspHouseRequest || streetId !== this.__uspPrivate_kyiv.streetId || query !== this.__uspPrivate_kyiv.house.trim()) return;
    this.__uspPrivate_kyiv.houses = items; this.us("house");
  }
  async lookupGroup() {
    if (this.__uspPrivate_kyiv.busy || this.__uspPrivate_kyiv.applying || this.__uspPrivate_kyiv.streetId == null || !this.__uspPrivate_kyiv.house.trim()) return;
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en;
    const key = this.__uspAddressKey(), request = ++this.__uspGroupRequest;
    this.__uspPrivate_kyiv.busy = true; this.__uspPrivate_kyiv.message = "";
    this.__uspPrivate_kyiv.result = null; this.__uspPrivate_kyiv.groupAddress = null;
    this.__uspUpdateAddressActions();
    try {
      const data = await this.gj(`/api/group?street=${encodeURIComponent(this.__uspPrivate_kyiv.street)}&house=${encodeURIComponent(this.__uspPrivate_kyiv.house)}`);
      if (request !== this.__uspGroupRequest || key !== this.__uspAddressKey()) return;
      if (!data?.ok || !Number.isInteger(data.group) || data.group < 1 || data.group > 60 || !Number.isInteger(data.subgroup || 1) || (data.subgroup || 1) < 1 || (data.subgroup || 1) > 9) throw new Error("Invalid group");
      this.__uspPrivate_kyiv.result = data; this.__uspPrivate_kyiv.groupAddress = key;
    } catch (error) {
      if (request === this.__uspGroupRequest) this.__uspPrivate_kyiv.message = `${t.lookupError}: ${error.message}`;
    } finally {
      if (request === this.__uspGroupRequest) { this.__uspPrivate_kyiv.busy = false; this.__uspUpdateAddressActions(); }
    }
  }
  async applyGroup() {
    const key = this.__uspAddressKey();
    if (!this.__uspPrivate_kyiv.result || this.__uspPrivate_kyiv.groupAddress !== key || this.__uspPrivate_kyiv.busy || this.__uspPrivate_kyiv.applying) return;
    const t = STRINGS[this.__uspPrivate_locale] || STRINGS.en, code = this.__uspGroupCode();
    const entity = this.en(OUTAGE_GROUP_ENTITY);
    if (!entity) { this.__uspPrivate_kyiv.message = "Outage Group Code API unavailable"; this.__uspUpdateAddressActions(); return; }
    this.__uspPrivate_kyiv.applying = true; this.__uspUpdateAddressActions();
    try {
      const ok = await this.post(entity, "set", {value:code});
      if (key === this.__uspAddressKey()) this.__uspPrivate_kyiv.message = ok ? `${t.groupApplied}: ${code}` : t.saveError;
    } finally { this.__uspPrivate_kyiv.applying = false; this.__uspUpdateAddressActions(); }
  }
  /** Confirm reset actions and keep one pending request per button. */
  __uspButtonControl(entity, n) {
    const pending = this.__uspPrivate_pendingButtons, id = entity.id;
    const b = node("button", "", n);
    b.disabled = pending.has(id);
    b.onclick = async () => {
      if (pending.has(id)) return;
      if (/^(Restart|Reset Network|Reset Integrations)$/.test(entity.name) && !confirm(n + "?")) return;
      pending.add(id); b.disabled = true;
      try {
        if (await this.post(entity, "press") && entity.name === "Repeat Setup") this.setTab("setup");
      } finally { pending.delete(id); this.patch(id); }
    };
    return b;
  }
  card(entity) {
    const t=STRINGS[this.__uspPrivate_locale]||STRINGS.en,n=label(this.__uspPrivate_locale,entity.name);
    const control=["light","select","number","text","switch","button"].includes(entity.domain);
    const card=node("article",`usp-card${control?" control":""}`);
    card.dataset.entityId=entity.id;card.append(node("div","usp-name",n));
    if (entity.domain === "select") {
      const select = node("select");
      for (const option of entity.option || []) { const item = node("option", "", label(this.__uspPrivate_locale, option)); item.value = option; if (option === entity.value || option === entity.state) item.selected = true; select.append(item); }
      select.onchange = () => this.post(entity, "set", {option: select.value}); card.append(select);
    } else if (entity.domain === "number") {
      const input = node("input"); input.type = "number"; input.min = entity.min_value ?? ""; input.max = entity.max_value ?? ""; input.step = entity.step ?? "1"; input.value = entity.value ?? "";
      input.onchange = () => this.post(entity, "set", {value: input.value}); input.onblur = () => this.patch(entity.id); card.append(input);
    } else if (entity.domain === "text" && entity.name === "Network SSID") {
      this.wifiControl(entity,t,card);
    } else if (entity.domain === "text") {
      card.append(this.__uspTextControl(entity, t));
    } else if (entity.domain === "switch") {
      const on = String(entity.state).toUpperCase() == "ON" || entity.value === true; const row = node("div", "usp-row"); row.append(node("span", `usp-pill ${on ? "good" : ""}`, on ? t.on : t.off)); const b = node("button", "", on ? t.off : t.on); b.onclick = () => this.post(entity, on ? "turn_off" : "turn_on"); row.append(b); card.append(row);
    } else if (entity.domain === "button") {
      card.append(this.__uspButtonControl(entity, n));
    } else if (entity.domain === "light") {
      const brightness = Number(entity.brightness ?? 255); const input = node("input"); input.type = "range"; input.min = "1"; input.max = "255"; input.value = String(brightness); const value = node("div", "usp-muted", `${Math.round(brightness / 2.55)}%`); input.oninput = () => value.textContent = `${Math.round(Number(input.value) / 2.55)}%`; input.onchange = () => this.post(entity, "turn_on", {brightness: input.value}); input.onblur = () => this.patch(entity.id); card.append(input, value);
    } else {
      const value = entity.state ?? entity.value ?? "—", shown = entity.name === "Wi-Fi SSID" ? safeSsidDisplay(value) : value; const valueEl = node("div", "usp-value");
      if (entity.name.endsWith("Status") || entity.name === "Alert Live State" || entity.name === "Alert Map Source") valueEl.append(node("span", `usp-pill ${badgeClass(value)}`, statusText(this.__uspPrivate_locale, value))); else valueEl.textContent = shown;
      if (value === "NEPTUN") {
        const attribution = node("a", "usp-muted", "neptun.in.ua");
        attribution.href = "https://neptun.in.ua/";
        valueEl.append(attribution);
      }
      card.append(valueEl); if (entity.uom && !String(value).includes(entity.uom)) card.append(node("div", "usp-muted", entity.uom));
    }
    const a=card.querySelector("input,select");if(a){a.ariaLabel=n;a.dataset.focusKey=entity.id;}
    this.__uspPrivate_entityCards.set(entity.id,card);return card;
  }
}
customElements.define("esp-app", UspApp);
