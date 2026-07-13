// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "GlobalShortcut.h"

#include "AudioInput.h"
#include "Channel.h"
#include "ClientUser.h"
#include "Database.h"
#include "EnvUtils.h"
#include "MainWindow.h"
#include "MumbleConstants.h"
#include "ServerHandler.h"
#include "Global.h"

#ifdef Q_OS_MAC
#	include <ApplicationServices/ApplicationServices.h>
#	include <QtCore/QOperatingSystemVersion>
#endif

#include <cassert>
#include <chrono>

GlobalShortcutEngine *GlobalShortcutEngine::engine = nullptr;
static const QString UPARROW = QString::fromUtf8("\xE2\x86\x91 ");


/**
 * This function returns a textual representation of the given shortcut target st.
 */
QString ShortcutTargetWidget::targetString(const ShortcutTarget &st) {
	if (st.bCurrentSelection) {
		return tr("Current selection");
	} else if (st.bUsers) {
		if (!st.qlUsers.isEmpty()) {
			QMap< QString, QString > hashes;

			QReadLocker lock(&ClientUser::c_qrwlUsers);
			for (ClientUser *p : ClientUser::c_qmUsers) {
				if (!p->qsHash.isEmpty()) {
					hashes.insert(p->qsHash, p->qsName);
				}
			}

			QStringList users;
			for (const QString &hash : st.qlUsers) {
				QString name;
				if (hashes.contains(hash)) {
					name = hashes.value(hash);
				} else {
					name = Global::get().db->getFriend(hash);
					if (name.isEmpty())
						name = QString::fromLatin1("#%1").arg(hash);
				}
				users << name;
			}

			users.sort();
			return users.join(tr(", "));
		}
	} else {
		if (st.iChannel < 0) {
			switch (st.iChannel) {
				case SHORTCUT_TARGET_ROOT:
					return tr("Root");
				case SHORTCUT_TARGET_PARENT:
					return tr("Parent");
				case SHORTCUT_TARGET_CURRENT:
					return tr("Current");
				default:
					if (st.iChannel <= SHORTCUT_TARGET_PARENT_SUBCHANNEL)
						return (UPARROW
								+ tr("Subchannel #%1").arg(SHORTCUT_TARGET_PARENT_SUBCHANNEL + 1 - st.iChannel));
					else
						return tr("Subchannel #%1").arg(SHORTCUT_TARGET_CURRENT - st.iChannel);
			}
		} else {
			Channel *c = Channel::get(static_cast< unsigned int >(st.iChannel));
			if (c)
				return c->qsName;
			else
				return tr("Invalid");
		}
	}
	return tr("Empty");
}


GlobalShortcutEngine::GlobalShortcutEngine(QObject *p) : QThread(p) {
	bNeedRemap = true;
	needRemap();
}

GlobalShortcutEngine::~GlobalShortcutEngine() {
	QSet< ShortcutKey * > qs;
	for (const QList< ShortcutKey * > &ql : qlShortcutList) {
		qs += QSet< ShortcutKey * >(ql.begin(), ql.end());
	}

	for (ShortcutKey *sk : qs) {
		delete sk;
	}
}

void GlobalShortcutEngine::remap() {
	bNeedRemap = false;

	QSet< ShortcutKey * > qs;
	for (const QList< ShortcutKey * > &ql : qlShortcutList) {
		qs += QSet< ShortcutKey * >(ql.begin(), ql.end());
	}

	for (ShortcutKey *sk : qs) {
		delete sk;
	}

	qlButtonList.clear();
	qlShortcutList.clear();
	qlDownButtons.clear();

	for (const Shortcut &sc : Global::get().s.qlShortcuts) {
		GlobalShortcut *gs = qmShortcuts.value(sc.iIndex);
		if (gs && !sc.qlButtons.isEmpty()) {
			ShortcutKey *sk = new ShortcutKey;
			sk->s           = sc;
			sk->iNumUp      = sc.qlButtons.count();
			sk->gs          = gs;

			for (const QVariant &button : sc.qlButtons) {
				auto idx = qlButtonList.indexOf(button);
				if (idx == -1) {
					qlButtonList << button;
					qlShortcutList << QList< ShortcutKey * >();
					idx = qlButtonList.count() - 1;
				}
				qlShortcutList[idx] << sk;
			}
		}
	}
}

void GlobalShortcutEngine::run() {
}

bool GlobalShortcutEngine::canSuppress() {
	return false;
}

void GlobalShortcutEngine::setEnabled(bool) {
}

bool GlobalShortcutEngine::enabled() {
	return true;
}

bool GlobalShortcutEngine::canDisable() {
	return false;
}

void GlobalShortcutEngine::resetMap() {
	tReset.restart();
	qlActiveButtons.clear();
}

void GlobalShortcutEngine::needRemap() {
}

/**
 * This function gets called internally to update the state
 * of a button.
 *
 * @return True if button is suppressed, otherwise false
 */
bool GlobalShortcutEngine::handleButton(const QVariant &button, bool down) {
	bool already = qlDownButtons.contains(button);
	if (already == down)
		return qlSuppressed.contains(button);
	if (down)
		qlDownButtons << button;
	else
		qlDownButtons.removeAll(button);

	if (tReset.elapsed() > std::chrono::milliseconds(100)) {
		if (down) {
			qlActiveButtons.removeAll(button);
			qlActiveButtons << button;
		}
		emit buttonPressed(!down);
	}

	if (down) {
		AudioInputPtr ai = Global::get().ai;
		if (ai.get()) {
			// XXX: This is a data race: we write to ai->activityState
			// (accessed by the AudioInput thread) from the main thread.
			if (ai->activityState == AudioInput::ActivityStateIdle) {
				ai->activityState = AudioInput::ActivityStateReturnedFromIdle;
			}
			ai->tIdle.restart();
		}
	}

	const auto idx = qlButtonList.indexOf(button);
	if (idx == -1)
		return false;

	bool suppress = false;

	for (ShortcutKey *sk : qlShortcutList.at(idx)) {
		if (down) {
			sk->iNumUp--;
			if (sk->iNumUp == 0) {
				GlobalShortcut *gs = sk->gs;
				if (sk->s.bSuppress) {
					suppress = true;
					qlSuppressed << button;
				}
				if (!gs->qlActive.contains(sk->s.qvData)) {
					gs->qlActive << sk->s.qvData;
					emit gs->triggered(true, sk->s.qvData);
					emit gs->down(sk->s.qvData);
				}
			} else if (sk->iNumUp < 0) {
				sk->iNumUp = 0;
			}
		} else {
			if (qlSuppressed.contains(button)) {
				suppress = true;
				qlSuppressed.removeAll(button);
			}
			sk->iNumUp++;
			if (sk->iNumUp == 1) {
				GlobalShortcut *gs = sk->gs;
				if (gs->qlActive.contains(sk->s.qvData)) {
					gs->qlActive.removeAll(sk->s.qvData);
					emit gs->triggered(false, sk->s.qvData);
				}
			} else if (sk->iNumUp > sk->s.qlButtons.count()) {
				sk->iNumUp = sk->s.qlButtons.count();
			}
		}
	}
	return suppress;
}

void GlobalShortcutEngine::add(GlobalShortcut *gs) {
	if (!GlobalShortcutEngine::engine) {
		GlobalShortcutEngine::engine = GlobalShortcutEngine::platformInit();
		GlobalShortcutEngine::engine->setEnabled(Global::get().s.bShortcutEnable);
	}

	GlobalShortcutEngine::engine->qmShortcuts.insert(gs->idx, gs);
	GlobalShortcutEngine::engine->bNeedRemap = true;
	GlobalShortcutEngine::engine->needRemap();
}

void GlobalShortcutEngine::remove(GlobalShortcut *gs) {
	engine->qmShortcuts.remove(gs->idx);
	engine->bNeedRemap = true;
	engine->needRemap();
	if (engine->qmShortcuts.isEmpty()) {
		delete engine;
		GlobalShortcutEngine::engine = nullptr;
	}
}

GlobalShortcut::GlobalShortcut(QObject *p, int index, QString qsName, QVariant def) : QObject(p) {
	idx       = index;
	name      = qsName;
	qvDefault = def;
	GlobalShortcutEngine::add(this);
}

GlobalShortcut::~GlobalShortcut() {
	GlobalShortcutEngine::remove(this);
}
