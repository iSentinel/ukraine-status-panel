import "./return.js";

const API = "/usp/spotify/auth/";
const STATE = /^[A-Za-z0-9_-]{22}$/;
const STATES = new Set(["unknown","idle","awaiting_user","code_pending","exchanging","connected","reauth","error"]);
const CHALLENGE = /^[A-Za-z0-9_-]{43}$/;
const SAFE = /^[\x20-\x7e]+$/;
const WORDS = {
  en: {
    back:"Back to panel", clientLabel:"Client ID", clientHelp:"Copy the public Client ID from your Spotify app settings.",
    redirectLabel:"HTTPS return address", redirectHelp:"Use the exact HTTPS address registered in Spotify.",
    connect:"Sign in with Spotify", cancel:"Cancel sign-in", open:"Open sign-in in a separate window",
    manualTitle:"Return a sign-in result", manualLabel:"Paste the result copied from the return page.",
    submit:"Submit result", privacy:"No password is requested here. Sign-in is completed on Spotify.",
    docs:"Spotify app setup", unavailable:"Spotify sign-in is currently unavailable.",
    idle:"Not connected", awaiting_user:"Complete sign-in in the Spotify window.",
    code_pending:"Finishing connection…", exchanging:"Finishing connection…", connected:"Connected",
    reauth:"Sign in again to reconnect.", error:"Connection was not completed.",
    unknown:"Checking connection…", manual:"You can return the result manually below.", denied:"Sign-in was cancelled.",
    badConfiguration:"Enter a valid Client ID and the registered HTTPS return address.",
    badResult:"Paste the complete result copied from the return page.",
    callback_rejected:"The sign-in result is invalid or expired. Start again.",
    authorization_active:"A sign-in is already in progress. Finish it or cancel.",
    busy:"The panel is busy. Check its status before trying again.",
    network:"The request was not confirmed. Check the connection status before starting again.",
    session_unavailable:"Sign-in could not start. Try again later."
  },
  ru: {
    back:"К панели", clientLabel:"Client ID", clientHelp:"Скопируйте открытый Client ID из настроек приложения Spotify.",
    redirectLabel:"HTTPS-адрес возврата", redirectHelp:"Укажите точный HTTPS-адрес, зарегистрированный в Spotify.",
    connect:"Войти через Spotify", cancel:"Отменить вход", open:"Открыть вход в отдельном окне",
    manualTitle:"Вернуть результат входа", manualLabel:"Вставьте результат, скопированный со страницы возврата.",
    submit:"Передать результат", privacy:"Пароль здесь не запрашивается. Вход выполняется на сайте Spotify.",
    docs:"Настройка приложения Spotify", unavailable:"Вход в Spotify сейчас недоступен.",
    idle:"Не подключено", awaiting_user:"Завершите вход в окне Spotify.",
    code_pending:"Завершаем подключение…", exchanging:"Завершаем подключение…", connected:"Подключено",
    reauth:"Для подключения войдите снова.", error:"Подключение не завершено.",
    unknown:"Проверяем подключение…", manual:"Результат можно перенести вручную ниже.", denied:"Вход отменён.",
    badConfiguration:"Укажите верный Client ID и зарегистрированный HTTPS-адрес возврата.",
    badResult:"Вставьте полный результат со страницы возврата.",
    callback_rejected:"Результат входа неверен или устарел. Начните заново.",
    authorization_active:"Вход уже выполняется. Завершите или отмените его.",
    busy:"Панель занята. Проверьте состояние перед повторной попыткой.",
    network:"Запрос не подтверждён. Проверьте состояние подключения перед новым входом.",
    session_unavailable:"Не удалось начать вход. Повторите позже."
  },
  uk: {
    back:"До панелі", clientLabel:"Client ID", clientHelp:"Скопіюйте відкритий Client ID з налаштувань застосунку Spotify.",
    redirectLabel:"HTTPS-адреса повернення", redirectHelp:"Вкажіть точну HTTPS-адресу, зареєстровану в Spotify.",
    connect:"Увійти через Spotify", cancel:"Скасувати вхід", open:"Відкрити вхід в окремому вікні",
    manualTitle:"Повернути результат входу", manualLabel:"Вставте результат, скопійований зі сторінки повернення.",
    submit:"Передати результат", privacy:"Пароль тут не запитується. Вхід відбувається на сайті Spotify.",
    docs:"Налаштування застосунку Spotify", unavailable:"Вхід у Spotify зараз недоступний.",
    idle:"Не підключено", awaiting_user:"Завершіть вхід у вікні Spotify.",
    code_pending:"Завершуємо підключення…", exchanging:"Завершуємо підключення…", connected:"Підключено",
    reauth:"Для підключення увійдіть знову.", error:"Підключення не завершено.",
    unknown:"Перевіряємо підключення…", manual:"Результат можна перенести вручну нижче.", denied:"Вхід скасовано.",
    badConfiguration:"Вкажіть правильний Client ID і зареєстровану HTTPS-адресу повернення.",
    badResult:"Вставте повний результат зі сторінки повернення.",
    callback_rejected:"Результат входу хибний або застарілий. Почніть знову.",
    authorization_active:"Вхід уже триває. Завершіть або скасуйте його.",
    busy:"Панель зайнята. Перевірте стан перед повторною спробою.",
    network:"Запит не підтверджено. Перевірте стан підключення перед новим входом.",
    session_unavailable:"Не вдалося почати вхід. Спробуйте пізніше."
  }
};

function validRedirect(value) {
  try {
    const uri = new URL(value);
    return value.startsWith("https://") && value.length <= 256 && !/[\s\\?#]/.test(value) &&
      uri.protocol === "https:" && !uri.username && !uri.password &&
      uri.port !== "0" && !!uri.hostname;
  } catch {return false;}
}

function authorizeUrl(value, client, redirect) {
  const uri = new URL(value);
  const p = uri.searchParams;
  for (const key of ["state","code_challenge","client_id","redirect_uri","response_type","code_challenge_method"])
    if (p.getAll(key).length !== 1) throw new Error("badConfiguration");
  if (uri.origin !== "https://accounts.spotify.com" || uri.pathname !== "/authorize" ||
      uri.username || uri.password || uri.hash || p.get("response_type") !== "code" ||
      p.get("code_challenge_method") !== "S256" || !STATE.test(p.get("state") || "") ||
      !CHALLENGE.test(p.get("code_challenge") || "") || p.get("client_id") !== client ||
      p.get("redirect_uri") !== redirect) throw new Error("badConfiguration");
  return uri;
}

function manualResult(value) {
  if (value.length > 4096) throw new Error("badResult");
  let data;
  try {data=JSON.parse(value);} catch {throw new Error("badResult");}
  if (!data || Array.isArray(data) || typeof data !== "object" ||
      Object.keys(data).some(k=>!["type","state","code","error"].includes(k)) ||
      data.type !== "usp-spotify-callback" || typeof data.state !== "string" || !STATE.test(data.state))
    throw new Error("badResult");
  if ((Object.hasOwn(data,"code") && typeof data.code!=="string") ||
      (Object.hasOwn(data,"error") && typeof data.error!=="string")) throw new Error("badResult");
  const code=typeof data.code === "string" ? data.code : null;
  const error=typeof data.error === "string" ? data.error : null;
  if ((code === null) === (error === null)) throw new Error("badResult");
  const result=code === null ? error : code;
  if (!result || !SAFE.test(result) || result.length > (code === null ? 128 : 1024))
    throw new Error("badResult");
  return code === null ? {state:data.state,error} : {state:data.state,code};
}

/** Only local authenticated requests run here; POST is never automatically retried. */
async function request(route, fields) {
  const controller=new AbortController(), timer=setTimeout(()=>controller.abort(),5000);
  try {
    const options={credentials:"same-origin",cache:"no-store",signal:controller.signal};
    if (fields !== undefined) Object.assign(options,{
      method:"POST",headers:{"Content-Type":"text/plain;charset=UTF-8"},
      body:new URLSearchParams(fields).toString()
    });
    const response=await fetch(API+route,options), text=await response.text();
    if (text.length > 8192) throw new Error("network");
    let result;
    try {result=JSON.parse(text);} catch {throw new Error("network");}
    if (!result || typeof result !== "object") throw new Error("network");
    if (!response.ok || result.ok !== true) {
      if(result.error==="exchange_unavailable")throw new Error("unavailable");
      if(result.error==="invalid_configuration")throw new Error("badConfiguration");
      const error = ["callback_rejected","authorization_active","busy","session_unavailable"].includes(result.error) ?
        result.error : "network";
      throw new Error(error);
    }
    return result;
  } finally {clearTimeout(timer);}
}

class ConfigurationPage {
  constructor() {
    this.language=Object.hasOwn(WORDS,(navigator.language||"en").slice(0,2)) ?
      navigator.language.slice(0,2) : "en";
    this.remote={state:"unknown",exchange_available:false};
    this.applicationApplied=false;
    this.revision=0;this.errorFromPoll=false;this.busy=false;this.channel=null;this.popup=null;this.errorKey="";this.noteKey="";
    this.ids=Object.fromEntries(["client","redirect","connect","cancel","manual","submit-result","open-login","error","hint","status"]
      .map(id=>[id,document.getElementById(id)]));
    document.getElementById("configuration").addEventListener("submit",e=>{e.preventDefault();this.begin();});
    this.ids.cancel.addEventListener("click",()=>this.cancel());
    this.ids["submit-result"].addEventListener("click",()=>this.submitManual());
    this.ids.manual.addEventListener("input",()=>this.render());
    this.translate();
    this.poll();this.timer=setInterval(()=>this.poll(),3000);
    addEventListener("pagehide",()=>this.dispose(),{once:true});
  }



  applyApplication(application) {
    if (this.applicationApplied || !application ||
        typeof application.client_id !== "string" ||
        !/^[A-Za-z0-9]{1,128}$/.test(application.client_id) ||
        typeof application.redirect_uri !== "string" || !validRedirect(application.redirect_uri))
      return;
    if (this.ids.client.value || this.ids.redirect.value ||
        [this.ids.client,this.ids.redirect].includes(document.activeElement)) return;
    this.ids.client.value=application.client_id;
    this.ids.redirect.value=application.redirect_uri;
    document.getElementById("application-fields").hidden=true;
    this.applicationApplied=true;
  }

  translate() {
    const words=WORDS[this.language];document.documentElement.lang=this.language;
    const labels={"back":"back","client-label":"clientLabel","client-help":"clientHelp",
      "redirect-label":"redirectLabel","redirect-help":"redirectHelp","connect":"connect",
      "cancel":"cancel","open-login":"open","manual-title":"manualTitle","manual-label":"manualLabel",
      "submit-result":"submit","privacy":"privacy","docs":"docs"};
    for (const [id,key] of Object.entries(labels)) document.getElementById(id).textContent=words[key];
    this.render();
  }

  render() {
    const words=WORDS[this.language], state=this.remote.state;
    const active=["awaiting_user","code_pending","exchanging"].includes(state);
    this.ids.connect.disabled=this.busy || !this.remote.exchange_available ||
      !["idle","reauth","error"].includes(state);
    this.ids.cancel.disabled=this.busy || !active;
    this.ids.client.readOnly=this.busy || active || state==="connected";
    this.ids.redirect.readOnly=this.ids.client.readOnly;
    this.ids["submit-result"].disabled=this.busy || !this.remote.exchange_available || state!=="awaiting_user" || !this.ids.manual.value.trim();
    const label=!this.remote.exchange_available && state!=="connected" ? words.unavailable :
      (Object.hasOwn(words,state) ? words[state] : words.unknown);
    if (this.ids.status.textContent !== label) this.ids.status.textContent=label;
    this.ids.status.dataset.state=state;
    this.ids.error.hidden=!this.errorKey;
    this.ids.error.textContent=words[this.errorKey] || "";
    this.ids.hint.hidden=!this.noteKey;
    this.ids.hint.textContent=words[this.noteKey] || "";
  }

  showError(error, fromPoll=false) {
    if(fromPoll && this.errorKey && !this.errorFromPoll)return;
    const key=Object.hasOwn(WORDS.en,error?.message) ? error.message : "network";
    if (!fromPoll && key === "network") this.remote.state="unknown";
    this.errorFromPoll=fromPoll;
    this.errorKey=key;
    this.noteKey="";this.render();
  }

  async poll() {
    if (this.busy || this.polling) return;
    this.polling=true;const revision=this.revision;
    try {
      const data=await request("status");
      if(revision!==this.revision)return;
      if(!STATES.has(data.state))throw new Error("network");
      if(this.errorFromPoll || (this.errorKey==="network" &&
          ["code_pending","exchanging","connected","idle"].includes(data.state))) {
        this.errorKey="";this.errorFromPoll=false;
      }
      if(data.state!=="awaiting_user") {
        this.ids["open-login"].hidden=true;
        this.channel?.close();this.channel=null;
      }
      this.remote={state:typeof data.state==="string"?data.state:"unknown",
        exchange_available:data.exchange_available===true};
      this.applyApplication(data.application);
      const language=Object.hasOwn(WORDS,data.language) ? data.language : this.language;
      if(language!==this.language){this.language=language;this.translate();}
      else this.render();
    } catch (error) {if(revision===this.revision)this.showError(error,true);}
    finally {this.polling=false;}
  }

  async begin() {
    if (this.ids.connect.disabled) return;
    const client=this.ids.client.value.trim(), redirect=this.ids.redirect.value.trim();
    if (!/^[A-Za-z0-9]{1,128}$/.test(client) || !validRedirect(redirect)) {
      this.showError(new Error("badConfiguration"));return;
    }
    this.channel?.close();this.channel=null;this.ids["open-login"].hidden=true;
    this.ids.client.value=client;this.ids.redirect.value=redirect;
    this.errorKey="";this.noteKey="";this.errorFromPoll=false;++this.revision;this.busy=true;this.render();
    try {this.popup=window.open("about:blank","usp-spotify-signin");} catch {this.popup=null;}
    try {
      const data=await request("begin",{client_id:client,redirect_uri:redirect});
      const uri=authorizeUrl(data.authorize_url,client,redirect);
      this.remote.state="awaiting_user";
      this.ids["open-login"].href=uri.href;this.ids["open-login"].hidden=false;
      this.attachPopup(uri,redirect);
    } catch (error) {
      if (this.popup && !this.popup.closed) this.popup.close();
      this.showError(error);
    } finally {this.busy=false;this.render();}
  }

  attachPopup(uri, redirect) {
    if (!this.popup || this.popup.closed) {this.noteKey="manual";return;}
    this.channel=new UspSpotifyAuthReturn({popup:this.popup,redirectUri:redirect,
      state:uri.searchParams.get("state"),onResult:r=>this.submit(r),
      onError:reason=>{
        if(reason==="expired"){
          this.ids["open-login"].hidden=true;this.showError(new Error("callback_rejected"));
        } else {this.noteKey="manual";this.render();}
      }});
    this.popup.location=uri.href;
  }

  async submit(result) {
    if (this.busy) return;
    this.busy=true;this.errorKey="";this.noteKey="";this.errorFromPoll=false;++this.revision;this.render();
    try {
      const data=await request("callback",result);
      if(!["denied","code_pending"].includes(data.state))throw new Error("network");
      this.ids.manual.value="";this.ids["open-login"].hidden=true;
      this.remote.state=data.state==="denied" ? "idle" : data.state;
      if(data.state==="denied")this.noteKey="denied";
    } catch (error) {this.showError(error);}
    finally {this.busy=false;this.render();}
  }

  submitManual() {
    if (this.ids["submit-result"].disabled) return;
    try {this.submit(manualResult(this.ids.manual.value));}
    catch (error) {this.showError(error);}
  }

  async cancel() {
    if (this.ids.cancel.disabled) return;
    this.busy=true;this.errorKey="";this.noteKey="";this.errorFromPoll=false;++this.revision;this.render();
    try {
      await request("cancel",{});
      this.channel?.close();this.channel=null;
      this.ids.manual.value="";this.ids["open-login"].hidden=true;
      this.remote.state="idle";
    } catch (error) {this.showError(error);}
    finally {this.busy=false;this.render();}
  }

  dispose() {
    clearInterval(this.timer);this.channel?.close();
  }
}
new ConfigurationPage();
