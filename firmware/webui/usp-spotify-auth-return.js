/* Local opener channel. HTTP submission belongs to the authenticated adapter. */
"use strict";
const STATE = /^[A-Za-z0-9_-]{22}$/;
const SAFE = /^[\x20-\x7e]+$/;

class AuthReturnChannel {
  constructor({popup, redirectUri, state, onResult, onError = () => {}}) {
    const uri = new URL(redirectUri);
    if (!popup || popup.closed || !STATE.test(state) || uri.protocol !== "https:" ||
        uri.username || uri.password || uri.search || uri.hash || redirectUri.includes("?") || redirectUri.includes("#") || (typeof onResult !== "function" || typeof onError !== "function"))
      throw new TypeError("Invalid authorization return configuration");
    this.popup = popup; this.origin = uri.origin; this.state = state;
    this.onResult = onResult; this.onError = onError; this.closed = false;
    this.receive = this.receive.bind(this);
    window.addEventListener("message", this.receive);
    this.timer = setTimeout(() => this.fail("expired"), 300000);
    this.watch = setInterval(() => {
      if (this.popup.closed) this.fail("popup_closed");
    }, 500);
  }

  receive(event) {
    if (this.closed || event.source !== this.popup || event.origin !== this.origin)
      return;
    const data = event.data;
    if (!data || typeof data !== "object") return;
    if (data.type === "usp-spotify-relay-ready") {
      this.popup.postMessage({type: "usp-spotify-bind", state: this.state}, this.origin);
      return;
    }
    if (data.type !== "usp-spotify-callback" || data.state !== this.state) return;
    const code = typeof data.code === "string" ? data.code : null;
    const error = typeof data.error === "string" ? data.error : null;
    if ((code === null) === (error === null)) return;
    const value = code === null ? error : code;
    if (!value || value.length > (code === null ? 128 : 1024) || !SAFE.test(value)) return;
    const result = code === null ? {state: this.state, error} : {state: this.state, code};
    const callback = this.onResult;
    this.close();
    callback(result);
  }

  fail(reason) {
    if (this.closed) return;
    const callback = this.onError;
    this.close();
    callback(reason);
  }

  close() {
    if (this.closed) return;
    this.closed = true;
    clearTimeout(this.timer); clearInterval(this.watch);
    window.removeEventListener("message", this.receive);
    this.state = ""; this.onResult = null; this.onError = null; this.popup = null;
  }
}
window.UspSpotifyAuthReturn = AuthReturnChannel;
