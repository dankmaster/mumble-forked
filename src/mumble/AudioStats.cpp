// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioStats.h"

#include "AudioInput.h"
#include "Utils.h"
#include "smallft.h"
#include "Global.h"

#include <QtGui/QPainter>

#include <cmath>

AudioBar::AudioBar(QWidget *p) : QWidget(p) {
	qcBelow  = Qt::yellow;
	qcAbove  = Qt::red;
	qcInside = Qt::green;

	iMin   = 0;
	iMax   = 32768;
	iBelow = 2000;
	iAbove = 22000;
	iValue = 1000;
	iPeak  = -1;
	setMinimumSize(100, 20);

	qlReplacableColors << Qt::yellow << Qt::red << Qt::green << Qt::blue;
	qlReplacementBrushes << Qt::BDiagPattern << Qt::DiagCrossPattern << Qt::NoBrush << Qt::FDiagPattern;
}

void AudioBar::paintEvent(QPaintEvent *) {
	QPainter p(this);

	if (isEnabled()) {
		qcBelow.setAlphaF(1.0f);
		qcAbove.setAlphaF(1.0f);
		qcInside.setAlphaF(1.0f);
	} else {
		qcBelow.setAlphaF(0.5f);
		qcAbove.setAlphaF(0.5f);
		qcInside.setAlphaF(0.5f);
	}

	if (iBelow > iAbove)
		iBelow = iAbove;

	if (iValue < iMin)
		iValue = iMin;
	else if (iValue > iMax)
		iValue = iMax;

	float scale = static_cast< float >(width()) / static_cast< float >(iMax - iMin);
	int h       = height();

	int val   = static_cast< int >(static_cast< float >(iValue) * scale + 0.5f);
	int below = static_cast< int >(static_cast< float >(iBelow) * scale + 0.5f);
	int above = static_cast< int >(static_cast< float >(iAbove) * scale + 0.5f);
	int max   = static_cast< int >(static_cast< float >(iMax) * scale + 0.5f);
	int min   = static_cast< int >(static_cast< float >(iMin) * scale + 0.5f);
	int peak  = static_cast< int >(static_cast< float >(iPeak) * scale + 0.5f);

	if (Global::get().s.bHighContrast) {
		// Draw monochrome representation
		QColor fg = QPalette().windowText().color();

		p.fillRect(0, 0, below, h,
				   QBrush(fg, qlReplacementBrushes.value(qlReplacableColors.indexOf(qcBelow), Qt::CrossPattern)));
		p.fillRect(below, 0, above - below, h,
				   QBrush(fg, qlReplacementBrushes.value(qlReplacableColors.indexOf(qcInside), Qt::NoBrush)));
		p.fillRect(above, 0, max - above, h,
				   QBrush(fg, qlReplacementBrushes.value(qlReplacableColors.indexOf(qcAbove), Qt::CrossPattern)));
		p.fillRect(0, 0, val, h, QBrush(fg, Qt::SolidPattern));

		p.drawRect(0, 0, max - 1, h - 1);
		p.drawLine(below, 0, below, h);
		p.drawLine(above, 0, above, h);
	} else {
		if (val <= below) {
			p.fillRect(0, 0, val, h, qcBelow);
			p.fillRect(val, 0, below - val, h, qcBelow.darker(300));
			p.fillRect(below, 0, above - below, h, qcInside.darker(300));
			p.fillRect(above, 0, max - above, h, qcAbove.darker(300));
		} else if (val <= above) {
			p.fillRect(0, 0, below, h, qcBelow);
			p.fillRect(below, 0, val - below, h, qcInside);
			p.fillRect(val, 0, above - val, h, qcInside.darker(300));
			p.fillRect(above, 0, max - above, h, qcAbove.darker(300));
		} else {
			p.fillRect(0, 0, below, h, qcBelow);
			p.fillRect(below, 0, above - below, h, qcInside);
			p.fillRect(above, 0, val - above, h, qcAbove);
			p.fillRect(val, 0, max - val, h, qcAbove.darker(300));
		}

		if ((peak >= min) && (peak <= max)) {
			if (peak <= below)
				p.setPen(qcBelow.lighter(150));
			else if (peak <= above)
				p.setPen(qcInside.lighter(150));
			else
				p.setPen(qcAbove.lighter(150));
			p.drawLine(peak, 0, peak, h);
		}
	}
}

AudioEchoWidget::AudioEchoWidget(QWidget *p) : QWidget(p) {
	setMinimumSize(100, 60);
}

static inline const QColor mapEchoToColor(float echo) {
	bool neg = (echo < 0.0f);
	echo     = fabsf(echo);

	float a, b, c;

	if (echo > 1.0f) {
		echo = 1.0f;
		c    = 0.5f;
	} else {
		c = 0.0f;
	}

	if (echo < 0.5f) {
		a = echo * 2.0f;
		b = 0.0f;
	} else {
		a = 1.0f;
		b = (echo - 0.5f) * 2.0f;
	}

	if (neg)
		return QColor::fromRgbF(a, b, c);
	else
		return QColor::fromRgbF(c, b, a);
}

void AudioEchoWidget::paintEvent(QPaintEvent *) {
	QPainter paint(this);

	paint.scale(width(), height());
	paint.fillRect(rect(), Qt::black);

	AudioInputPtr ai = Global::get().ai;
	if (!ai || !ai->sesEcho)
		return;

	ai->qmSpeex.lock();

	spx_int32_t sz;
	speex_echo_ctl(ai->sesEcho, SPEEX_ECHO_GET_IMPULSE_RESPONSE_SIZE, &sz);

	static std::vector< spx_int32_t > w;
	w.resize(static_cast< std::size_t >(sz));
	static std::vector< float > W;
	W.resize(static_cast< std::size_t >(sz));

	speex_echo_ctl(ai->sesEcho, SPEEX_ECHO_GET_IMPULSE_RESPONSE, w.data());

	ai->qmSpeex.unlock();

	constexpr unsigned int N = 160;
	constexpr unsigned int n = 2 * N;
	const unsigned int M     = static_cast< unsigned int >(sz) / n;

	drft_lookup d;
	mumble_drft_init(&d, n);

	for (unsigned int j = 0; j < M; j++) {
		for (unsigned int i = 0; i < n; i++)
			W[j * n + i] = static_cast< float >(w[j * n + i]) / static_cast< float >(n);
		mumble_drft_forward(&d, &W[j * n]);
	}

	mumble_drft_clear(&d);

	float xscale = 1.0f / static_cast< float >(N);
	float yscale = 1.0f / static_cast< float >(M);

	for (unsigned int j = 0; j < M; j++) {
		for (unsigned int i = 1; i < N; i++) {
			float xa = static_cast< float >(i) * xscale;
			float ya = static_cast< float >(j) * yscale;

			float xb = xa + xscale;
			float yb = ya + yscale;

			const QColor &c = mapEchoToColor(
				sqrtf(W[j * n + 2 * i] * W[j * n + 2 * i] + W[j * n + 2 * i - 1] * W[j * n + 2 * i - 1]) / 65536.f);
			paint.fillRect(QRectF(QPointF(xa, ya), QPointF(xb, yb)), c);
		}
	}

	QPolygonF poly;
	xscale = 1.0f / (2.0f * static_cast< float >(n));
	yscale = 1.0f / (200.0f * 32767.0f);
	for (unsigned int i = 0; i < 2 * n; i++) {
		poly << QPointF(static_cast< float >(i) * xscale, 0.5f + static_cast< float >(w[i]) * yscale);
	}

	paint.setPen(QPen(QBrush(QColor::fromRgbF(1.0f, 0.0f, 1.0f)), 0));
	paint.drawPolyline(poly);
}

AudioNoiseWidget::AudioNoiseWidget(QWidget *p) : QWidget(p) {
	setMinimumSize(100, 60);
}

void AudioNoiseWidget::paintEvent(QPaintEvent *) {
	QPainter paint(this);
	QPalette pal;

	paint.fillRect(rect(), pal.color(QPalette::Window));

	AudioInputPtr ai = Global::get().ai;
	if (!ai.get() || !ai->m_preprocessor)
		return;

	QPolygonF poly;

	ai->qmSpeex.lock();

	const AudioPreprocessor::psd_t ps    = ai->m_preprocessor.getPSD();
	const AudioPreprocessor::psd_t noise = ai->m_preprocessor.getNoisePSD();

	ai->qmSpeex.unlock();

	assert(ps.size() == noise.size());

	qreal sx, sy;

	sx = (static_cast< float >(width()) - 1.0f) / static_cast< float >(ps.size());
	sy = static_cast< float >(height()) - 1.0f;

	poly << QPointF(0.0f, height() - 1);
	float fftmul = 1.0 / (32768.0);
	for (unsigned int i = 0; i < static_cast< unsigned int >(noise.size()); i++) {
		qreal xp, yp;
		xp = i * sx;
		yp = sqrtf(sqrtf(static_cast< float >(noise[i]))) - 1.0f;
		yp = yp * fftmul;
		yp = qMin< qreal >(yp * 3000.0f, 1.0f);
		yp = (1 - yp) * sy;
		poly << QPointF(xp, yp);
	}

	poly << QPointF(width() - 1, height() - 1);
	poly << QPointF(0.0f, height() - 1);

	paint.setPen(Qt::blue);
	paint.setBrush(Qt::blue);
	paint.drawPolygon(poly);

	poly.clear();

	for (unsigned int i = 0; i < static_cast< unsigned int >(ps.size()); i++) {
		qreal xp, yp;
		xp = i * sx;
		yp = sqrtf(sqrtf(static_cast< float >(ps[i]))) - 1.0f;
		yp = yp * fftmul;
		yp = qMin(yp * 3000.0, 1.0);
		yp = (1 - yp) * sy;
		poly << QPointF(xp, yp);
	}

	paint.setPen(Qt::red);
	paint.drawPolyline(poly);
}
