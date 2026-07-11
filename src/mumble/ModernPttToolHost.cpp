// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ModernPttToolHost.h"

#include "Global.h"

#include <QtCore/QUrl>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QVBoxLayout>

namespace {
	const char pttToolHtml[] = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><style>
:root{color-scheme:dark;font-family:Inter,"Segoe UI",sans-serif}*{box-sizing:border-box}
html,body{width:100%;height:100%;margin:0;overflow:hidden;background:#11151c;color:#eef3fb}
body{padding:10px}.ptt{width:100%;height:100%;border:1px solid #39465a;border-radius:16px;
background:linear-gradient(145deg,#202a38,#151b24);color:inherit;font:700 16px inherit;cursor:pointer;
box-shadow:0 12px 32px #0008;transition:transform .08s ease,border-color .08s ease,background .08s ease}
.ptt:hover{border-color:#66a9ff}.ptt:focus-visible{outline:2px solid #66a9ff;outline-offset:2px}
.ptt.is-pressed{transform:scale(.97);border-color:#ff657a;background:linear-gradient(145deg,#5a2430,#28161c)}
.hint{display:block;margin-top:5px;color:#9eabc0;font-size:11px;font-weight:500}
</style><script src="qrc:///qtwebchannel/qwebchannel.js"></script></head><body>
<button id="ptt" class="ptt" type="button">Push to talk<span class="hint">Hold to transmit</span></button>
<script>
(function(){let host=null,pressed=false;const button=document.getElementById('ptt');
function setPressed(next){next=!!next;if(pressed===next)return;pressed=next;button.classList.toggle('is-pressed',next);
if(host&&typeof host.setPressed==='function')host.setPressed(next)}
button.addEventListener('pointerdown',function(e){e.preventDefault();button.setPointerCapture(e.pointerId);setPressed(true)});
['pointerup','pointercancel','lostpointercapture'].forEach(function(name){button.addEventListener(name,function(){setPressed(false)})});
window.addEventListener('blur',function(){setPressed(false)});document.addEventListener('visibilitychange',function(){if(document.hidden)setPressed(false)});
new QWebChannel(qt.webChannelTransport,function(channel){host=channel.objects.pttHost||null});
})();</script></body></html>)HTML";
}

ModernPttToolHost::ModernPttToolHost(QWidget *parent) : QWidget(parent) {
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
	setWindowTitle(tr("Push to talk"));
	setMinimumSize(190, 110);
	resize(240, 140);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_view    = new QWebEngineView(this);
	m_channel = new QWebChannel(m_view->page());
	m_channel->registerObject(QStringLiteral("pttHost"), this);
	m_view->page()->setWebChannel(m_channel);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_view->setHtml(QString::fromUtf8(pttToolHtml), QUrl(QStringLiteral("qrc:/")));
	layout->addWidget(m_view);

	if (!Global::get().s.qbaPTTButtonWindowGeometry.isEmpty()) {
		restoreGeometry(Global::get().s.qbaPTTButtonWindowGeometry);
	}
}

void ModernPttToolHost::setPressed(const bool pressed) {
	if (m_pressed == pressed) {
		return;
	}
	m_pressed = pressed;
	emit triggered(pressed, QVariant());
}

void ModernPttToolHost::release() {
	setPressed(false);
}

void ModernPttToolHost::closeEvent(QCloseEvent *event) {
	release();
	Global::get().s.qbaPTTButtonWindowGeometry = saveGeometry();
	QWidget::closeEvent(event);
}

void ModernPttToolHost::focusOutEvent(QFocusEvent *event) {
	release();
	QWidget::focusOutEvent(event);
}

void ModernPttToolHost::hideEvent(QHideEvent *event) {
	release();
	QWidget::hideEvent(event);
}
