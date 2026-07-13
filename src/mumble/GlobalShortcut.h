// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_GLOBALSHORTCUT_H_
#define MUMBLE_MUMBLE_GLOBALSHORTCUT_H_

#include <QtCore/QThread>
#include <QtCore/QtGlobal>
#include <QtCore/QCoreApplication>

#include "Channel.h"
#include "Settings.h"
#include "Timer.h"

class GlobalShortcut : public QObject {
	friend class GlobalShortcutEngine;

private:
	Q_OBJECT
	Q_DISABLE_COPY(GlobalShortcut)
protected:
	QList< QVariant > qlActive;
signals:
	void down(QVariant);
	void triggered(bool, QVariant);

public:
	QString qsToolTip;
	QString qsWhatsThis;
	QString name;
	QVariant qvDefault;
	int idx;

	GlobalShortcut(QObject *parent, int index, QString qsName, QVariant def = QVariant());
	~GlobalShortcut() Q_DECL_OVERRIDE;

	bool active() const { return !qlActive.isEmpty(); }
};

enum ShortcutTargetTypes {
	SHORTCUT_TARGET_ROOT              = -1,
	SHORTCUT_TARGET_PARENT            = -2,
	SHORTCUT_TARGET_CURRENT           = -3,
	SHORTCUT_TARGET_SUBCHANNEL        = -4,
	SHORTCUT_TARGET_PARENT_SUBCHANNEL = -12
};

class ShortcutTargetWidget final {
	Q_DECLARE_TR_FUNCTIONS(ShortcutTargetWidget)
public:
	static QString targetString(const ShortcutTarget &target);
};


struct ShortcutKey {
	Shortcut s;
	qsizetype iNumUp;
	GlobalShortcut *gs;
};

/**
 * Creates a background thread which handles global shortcut behaviour. This class inherits
 * a system unspecific interface and basic functionality to the actually used native backend
 * classes (GlobalShortcutPlatform).
 *
 * @see GlobalShortcutX
 * @see GlobalShortcutMac
 * @see GlobalShortcutWin
 */
class GlobalShortcutEngine : public QThread {
private:
	Q_OBJECT
	Q_DISABLE_COPY(GlobalShortcutEngine)
public:
	struct ButtonInfo {
		QString device;
		QString devicePrefix;
		QString name;

		ButtonInfo() : device(tr("Unknown")), name(tr("Unknown")) {}
	};

	bool bNeedRemap;
	Timer tReset;

	static GlobalShortcutEngine *engine;
	static GlobalShortcutEngine *platformInit();

	QHash< int, GlobalShortcut * > qmShortcuts;
	QList< QVariant > qlActiveButtons;
	QList< QVariant > qlDownButtons;
	QList< QVariant > qlSuppressed;

	QList< QVariant > qlButtonList;
	QList< QList< ShortcutKey * > > qlShortcutList;

	GlobalShortcutEngine(QObject *p = nullptr);
	~GlobalShortcutEngine() Q_DECL_OVERRIDE;
	void resetMap();
	void remap();
	virtual void needRemap();
	void run() Q_DECL_OVERRIDE;

	bool handleButton(const QVariant &, bool);
	static void add(GlobalShortcut *);
	static void remove(GlobalShortcut *);

	virtual bool canDisable();
	virtual bool canSuppress();
	virtual bool enabled();
	virtual void setEnabled(bool b);

	virtual ButtonInfo buttonInfo(const QVariant &) = 0;
signals:
	void buttonPressed(bool last);
};

#endif
