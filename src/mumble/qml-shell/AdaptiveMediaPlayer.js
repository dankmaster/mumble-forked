/*
 * Local, bridge-free direct/adaptive media bootstrap for the isolated
 * WebEngineQuick profile. The source URL and MIME are carried only in this
 * document's base64url fragment. Network policy remains enforced by
 * MediaRequestInterceptor.
 */
(function() {
  "use strict";

  const state = window.__mumbleAdaptiveState = {
    adaptive: false,
    error: "",
    manifestOrigin: "",
    mediaMime: "",
    ready: false,
    sourceOrigin: "",
    stage: "initializing"
  };
  let player = null;

  function errorText(error) {
    if (error && error.detail) {
      const detail = error.detail;
      const code = Number(detail.code || 0);
      const data = Array.isArray(detail.data) ? detail.data.join(", ") : "";
      return [ code ? "Shaka " + code : "Media playback failed", data ]
        .filter(Boolean).join(": ");
    }
    return String(error && error.message || error || "Media playback failed");
  }

  function fail(error) {
    state.ready = false;
    state.stage = "error";
    state.error = errorText(error).slice(0, 1024);
    document.documentElement.dataset.mumbleAdaptiveState = "error";
    console.error("Mumble adaptive media:", state.error);
  }

  function decodeRequest() {
    let value = String(window.location.hash || "").replace(/^#/, "")
      .replace(/-/g, "+").replace(/_/g, "/");
    while (value.length % 4) value += "=";
    const bytes = Uint8Array.from(window.atob(value), character => character.charCodeAt(0));
    const decoded = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    let payload = null;
    try {
      payload = JSON.parse(decoded);
    } catch (_) {
      // Accept the original manifest-only fragment during rolling upgrades.
      payload = {
        adaptive: true,
        mediaMime: "application/vnd.apple.mpegurl",
        sourceUrl: decoded
      };
    }
    const mediaMime = String(payload && payload.mediaMime || "")
      .split(";", 1)[0].trim().toLowerCase();
    const adaptive = Boolean(payload && payload.adaptive)
      || mediaMime === "application/vnd.apple.mpegurl"
      || mediaMime === "application/dash+xml";
    if (!adaptive && !/^(?:video|audio)\//.test(mediaMime)) {
      throw new Error("The direct media MIME type is not supported.");
    }
    const url = new URL(String(payload && payload.sourceUrl || ""));
    if (url.protocol !== "https:" || url.username || url.password
        || (url.port && url.port !== "443")) {
      throw new Error("The media URL is not a safe HTTPS source.");
    }
    return { adaptive: adaptive, mediaMime: mediaMime, sourceUrl: url };
  }

  function waitForDirectMedia(media) {
    return new Promise(function(resolve, reject) {
      let settled = false;
      const finish = function(callback, value) {
        if (settled) return;
        settled = true;
        media.removeEventListener("loadedmetadata", ready);
        media.removeEventListener("canplay", ready);
        media.removeEventListener("error", failed);
        callback(value);
      };
      const ready = function() {
        if (Number(media.readyState || 0) >= 1) finish(resolve);
      };
      const failed = function() {
        const detail = media.error && media.error.message
          ? media.error.message : "The direct media source could not be decoded.";
        finish(reject, new Error(detail));
      };
      media.addEventListener("loadedmetadata", ready);
      media.addEventListener("canplay", ready);
      media.addEventListener("error", failed);
      media.src = state.sourceUrl;
      media.load();
      ready();
    });
  }

  function installPrimaryClickPlayback(media) {
    media.addEventListener("click", function(event) {
      if (event.button !== 0 || !state.ready) return;
      const videoPresentation = media.videoWidth > 0
        || /^video\//.test(state.mediaMime) || state.adaptive;
      if (!videoPresentation) return;
      if (!media.paused && !media.ended) {
        media.pause();
        return;
      }
      if (media.ended) media.currentTime = 0;
      window.__mumbleMediaPlayError = "";
      const playback = media.play();
      if (playback && playback.catch) {
        playback.catch(function(error) {
          window.__mumbleMediaPlayError = errorText(error);
        });
      }
    });
  }

  async function initialize() {
    const request = decodeRequest();
    state.adaptive = request.adaptive;
    state.mediaMime = request.mediaMime;
    state.sourceOrigin = request.sourceUrl.origin;
    state.manifestOrigin = request.adaptive ? request.sourceUrl.origin : "";
    state.sourceUrl = request.sourceUrl.href;
    const media = document.getElementById("mumble-isolated-media");
    installPrimaryClickPlayback(media);

    if (request.adaptive) {
      state.stage = "attaching";
      if (!window.shaka || !shaka.Player) throw new Error("The bundled adaptive player is unavailable.");
      shaka.polyfill.installAll();
      if (!shaka.Player.isBrowserSupported()) {
        throw new Error("This WebEngine runtime does not support Media Source Extensions.");
      }

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
      await player.load(request.sourceUrl.href);
    } else {
      state.stage = "loading";
      await waitForDirectMedia(media);
    }

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
