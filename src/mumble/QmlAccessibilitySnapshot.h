// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_QMLACCESSIBILITYSNAPSHOT_H_
#define MUMBLE_MUMBLE_QMLACCESSIBILITYSNAPSHOT_H_

#include <QtCore/QVariantMap>

class QAccessibleInterface;
class QWindow;

struct QmlAccessibilitySnapshotLimits {
	int maximumDepth = 32;
	int maximumNodes = 2048;
	int maximumChildrenPerNode = 256;
	int maximumStringLength = 4096;
};

class QmlAccessibilitySnapshot {
public:
	static QVariantMap serialize(QWindow *window,
							 const QmlAccessibilitySnapshotLimits &limits = QmlAccessibilitySnapshotLimits());
	static QVariantMap serialize(QAccessibleInterface *root,
							 const QmlAccessibilitySnapshotLimits &limits = QmlAccessibilitySnapshotLimits());
};

#endif // MUMBLE_MUMBLE_QMLACCESSIBILITYSNAPSHOT_H_
