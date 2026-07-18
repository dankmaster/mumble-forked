/*
 * Local, bridge-free adaptive media bootstrap for the isolated WebEngineQuick
 * profile. The manifest URL is carried only in this document's base64url
 * fragment. Network policy remains enforced by MediaRequestInterceptor.
 */
(function() {
  "use strict";

  const state = window.__mumbleAdaptiveState = {
    error: "",
    manifestOrigin: "",
    ready: false,
    stage: "initializing"
  };
  let player = null;

  function errorText(error) {
    if (error && error.detail) {
      const detail = error.detail;
      const code = Number(detail.code || 0);
      const data = Array.isArray(detail.data) ? detail.data.join(", ") : "";
      return [ code ? "Shaka " + code : "Adaptive playback failed", data ]
        .filter(Boolean).join(": ");
    }
    return String(error && error.message || error || "Adaptive playback failed");
  }

  function fail(error) {
    state.ready = false;
    state.stage = "error";
    state.error = errorText(error).slice(0, 1024);
    document.documentElement.dataset.mumbleAdaptiveState = "error";
    console.error("Mumble adaptive media:", state.error);
  }

  function decodeManifestUrl() {
    let value = String(window.location.hash || "").replace(/^#/, "")
      .replace(/-/g, "+").replace(/_/g, "/");
    while (value.length % 4) value += "=";
    const bytes = Uint8Array.from(window.atob(value), character => character.charCodeAt(0));
    const decoded = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    const url = new URL(decoded);
    if (url.protocol !== "https:" || url.username || url.password
        || (url.port && url.port !== "443")) {
      throw new Error("The adaptive manifest URL is not a safe HTTPS source.");
    }
    return url;
  }

  async function initialize() {
    const manifestUrl = decodeManifestUrl();
    state.manifestOrigin = manifestUrl.origin;
    state.stage = "attaching";
    if (!window.shaka || !shaka.Player) throw new Error("The bundled adaptive player is unavailable.");
    shaka.polyfill.installAll();
    if (!shaka.Player.isBrowserSupported()) {
      throw new Error("This WebEngine runtime does not support Media Source Extensions.");
    }

    const media = document.getElementById("mumble-adaptive-media");
    player = new shaka.Player();
    window.__mumbleAdaptivePlayer = player;
    player.addEventListener("error", fail);
    await player.attach(media);
    player.configure({
      manifest: {
        retryParameters: {
          maxAttempts: 3,
          baseDelay: 250,
          backoffFactor: 2,
          fuzzFactor: 0.25,
          timeout: 10000,
          stallTimeout: 5000,
          connectionTimeout: 5000
        }
      },
      streaming: {
        bufferingGoal: 15,
        rebufferingGoal: 2,
        retryParameters: {
          maxAttempts: 3,
          baseDelay: 250,
          backoffFactor: 2,
          fuzzFactor: 0.25,
          timeout: 10000,
          stallTimeout: 5000,
          connectionTimeout: 5000
        }
      }
    });
    state.stage = "loading";
    await player.load(manifestUrl.href);
    state.error = "";
    state.ready = true;
    state.stage = "ready";
    document.documentElement.dataset.mumbleAdaptiveState = "ready";
  }

  window.addEventListener("beforeunload", function() {
    if (player) player.destroy();
    player = null;
    window.__mumbleAdaptivePlayer = null;
  }, { once: true });

  initialize().catch(fail);
})();
