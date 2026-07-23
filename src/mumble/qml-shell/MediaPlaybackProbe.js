.pragma library

function normalizedProvider(value) {
	return String(value || "").trim().toLowerCase()
}

function compactDetail(value) {
	return String(value || "").replace(/\s+/g, " ").trim().slice(0, 240)
}

function probeScript(provider, adaptiveExpected) {
	const providerLiteral = JSON.stringify(normalizedProvider(provider))
	return "(function(){"
		+ "const provider=" + providerLiteral + ";"
		+ "const readyState=String(document.readyState||'');"
		+ "const media=document.querySelector('video,audio');"
		+ "const yt=document.getElementById('movie_player')||document.querySelector('.html5-video-player');"
		+ "const adaptiveExpected=" + (adaptiveExpected ? "true" : "false") + ";"
		+ "const adaptive=adaptiveExpected?(window.__mumbleAdaptiveState||null):null;"
		+ "const adaptiveError=adaptive?String(adaptive.error||''):'';"
		+ "const selectorByProvider={"
		+ "youtube:'#movie_player,.html5-video-player,video',"
		+ "twitch:'[data-a-target=\"video-player\"],.video-player,video',"
		+ "streamable:'#video-player,.player,video',"
		+ "vimeo:'.vp-video-wrapper,.player,video',"
		+ "dailymotion:'#player,.dmp_PlayerRoot,[class*=\"Player\"],video',"
		+ "spotify:'[data-testid=\"embed-widget\"],[data-testid=\"play-button\"],main,button[aria-label*=\"Play\"]',"
		+ "facebook:'[data-testid*=\"video\"],[role=\"main\"],video',"
		+ "tiktok:'[data-e2e=\"embed-video\"],[class*=\"Embed\"],blockquote,video',"
		+ "instagram:'article,[role=\"main\"],blockquote,video',"
		+ "soundcloud:'#app,.playControls,.widget,button[title*=\"Play\"]',"
		+ "direct:'video,audio'};"
		+ "const providerSelector=selectorByProvider[provider]||'video,audio';"
		+ "const providerRoot=document.querySelector(providerSelector);"
		+ "const text=String(document.body&&document.body.innerText||'').replace(/\\s+/g,' ').trim().slice(0,1200);"
		+ "const lower=text.toLowerCase();"
		+ "const challenge=document.querySelector('.g-recaptcha,iframe[src*=\"recaptcha\"],iframe[src*=\"challenge\"],form[action*=\"challenge\"],input[name*=\"captcha\"]');"
		+ "const password=document.querySelector('input[type=\"password\"]');"
		+ "const explicitError=document.querySelector('.ytp-error,.ytp-error-content-wrap,.player-error,.error-page,[data-a-target=\"content-overlay-gate\"]');"
		+ "const challengeText=/(confirm (that )?you('| a)?re not a bot|sign in to confirm|verify (that )?you are human|bekräfta att du inte är en bot|logga in för att bekräfta|captcha)/.test(lower);"
		+ "const signInText=/(sign in|log in|logga in|sign-in required|login required)/.test(lower);"
		+ "const unavailableText=/(video (is )?unavailable|this content isn('|’)t available|content unavailable|innehållet är inte tillgängligt|videon är inte tillgänglig|access denied|forbidden|page isn('|’)t available|sidan är inte tillgänglig)/.test(lower);"
		+ "const youtubeTransport=provider==='youtube'&&!!yt&&typeof yt.getPlayerState==='function';"
		+ "const providerUi=!!providerRoot;"
		+ "const mediaPresent=!!media;"
		+ "const mediaReady=!!media&&Number(media.readyState||0)>=1;"
		+ "const socialVideo=provider==='instagram'||provider==='tiktok'||provider==='facebook';"
		+ "const transport=provider==='youtube'?youtubeTransport:((provider==='direct'||socialVideo)?mediaPresent:providerUi||mediaPresent);"
		+ "let blockedKind='';"
		+ "if(challenge||challengeText)blockedKind='verification';"
		+ "else if(password&&(!mediaPresent||signInText))blockedKind='sign-in';"
		+ "else if(explicitError||unavailableText)blockedKind='unavailable';"
		+ "let paused=true,position=0,duration=0;"
		+ "if(youtubeTransport){const state=Number(yt.getPlayerState());paused=state!==1&&state!==3;position=Number(yt.getCurrentTime&&yt.getCurrentTime()||0);duration=Number(yt.getDuration&&yt.getDuration()||0);}"
		+ "else if(media){paused=!!media.paused;position=Number(media.currentTime||0);duration=isFinite(media.duration)?Number(media.duration):0;}"
		+ "const playbackEvidence=transport&&(!paused||position>0.05);"
		+ "return {readyState:readyState,provider:provider,transport:transport,providerUi:providerUi,mediaPresent:mediaPresent,mediaReady:mediaReady,paused:paused,position:position,duration:duration,playbackEvidence:playbackEvidence,blockedKind:blockedKind,detail:text,adaptiveError:adaptiveError,title:String(document.title||'').slice(0,160)};"
		+ "})()"
}

function classify(value, provider, adaptiveExpected, attempt, maximumAttempts) {
	const result = value && typeof value === "object" ? value : ({})
	const readyState = String(result.readyState || "")
	const documentReady = readyState === "interactive" || readyState === "complete"
	const blockedKind = String(result.blockedKind || "")
	const adaptiveError = compactDetail(result.adaptiveError)
	const transport = result.transport === true
	const playbackEvidence = result.playbackEvidence === true
	const evidenceParts = []
	if (documentReady)
		evidenceParts.push("document")
	if (result.providerUi === true)
		evidenceParts.push("provider-ui")
	if (result.mediaPresent === true)
		evidenceParts.push("media")
	if (result.mediaReady === true)
		evidenceParts.push("media-ready")
	if (transport)
		evidenceParts.push("transport")
	if (playbackEvidence)
		evidenceParts.push("playback")

	if (adaptiveError.length > 0) {
		return {
			state: "failed",
			kind: "adaptive-renderer-failed",
			detail: adaptiveError,
			evidence: evidenceParts.join(","),
			transportVerified: false,
			playbackVerified: false
		}
	}
	if (blockedKind.length > 0) {
		return {
			state: "blocked",
			kind: blockedKind,
			detail: compactDetail(result.detail),
			evidence: evidenceParts.join(","),
			transportVerified: false,
			playbackVerified: false
		}
	}
	if (documentReady && transport
			&& (!adaptiveExpected || (result.mediaPresent === true && result.mediaReady === true))) {
		return {
			state: "verified",
			kind: "",
			detail: "",
			evidence: evidenceParts.join(","),
			transportVerified: true,
			playbackVerified: playbackEvidence
		}
	}
	if (Number(attempt || 0) >= Math.max(1, Number(maximumAttempts || 1))) {
		return {
			state: "failed",
			kind: adaptiveExpected ? "adaptive-renderer-timeout" : "provider-surface-timeout",
			detail: compactDetail(result.detail),
			evidence: evidenceParts.join(","),
			transportVerified: false,
			playbackVerified: false
		}
	}
	return {
		state: "pending",
		kind: "",
		detail: "",
		evidence: evidenceParts.join(","),
		transportVerified: false,
		playbackVerified: false
	}
}
