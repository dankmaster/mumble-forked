// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "mumble/PersistentChatHistoryModel.h"
#include "mumble/widgets/PersistentChatListWidget.h"

#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStyledItemDelegate>

namespace {
	PersistentChatHistoryRow makeMessageGroupRow(const QString &rowId) {
		PersistentChatHistoryRow row;
		row.kind      = PersistentChatHistoryRowKind::MessageGroup;
		row.rowId     = rowId;
		row.signature = rowId;
		row.messageGroup.emplace();
		return row;
	}

	class FixedHeightDelegate : public QStyledItemDelegate {
	public:
		explicit FixedHeightDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

		QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
			const QString rowId = index.data(PersistentChatHistoryModel::RowIdRole).toString();
			return QSize(320, rowId == QLatin1String("group:last") ? 600 : 100);
		}
	};

	class TestChatListWidget : public PersistentChatListWidget {
	public:
		using PersistentChatListWidget::PersistentChatListWidget;

		void forceLayout() {
			doItemsLayout();
			updateGeometries();
		}
	};

	void flushLayout(TestChatListWidget &view) {
		for (int i = 0; i < 3; ++i) {
			QApplication::processEvents(QEventLoop::AllEvents, 20);
			view.forceLayout();
		}
	}
}

class TestPersistentChatListWidget : public QObject {
	Q_OBJECT

private slots:
	void lastMessageMustReachViewportBottomToCountAsScrolledToBottom();
};

void TestPersistentChatListWidget::lastMessageMustReachViewportBottomToCountAsScrolledToBottom() {
	PersistentChatHistoryModel model;
	TestChatListWidget view;
	FixedHeightDelegate delegate(&view);

	view.setModel(&model);
	view.setItemDelegate(&delegate);
	view.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	view.resize(320, 240);
	model.setRows({ makeMessageGroupRow(QStringLiteral("group:first")),
					makeMessageGroupRow(QStringLiteral("group:last")) });
	view.show();
	flushLayout(view);

	QScrollBar *scrollBar = view.verticalScrollBar();
	QVERIFY(scrollBar);
	QVERIFY(scrollBar->maximum() > 120);

	scrollBar->setValue(120);
	flushLayout(view);

	QVERIFY(view.isRowVisible(QStringLiteral("group:last")));
	QVERIFY(!view.isScrolledToBottom());

	scrollBar->setValue(scrollBar->maximum());
	flushLayout(view);

	QVERIFY(view.isScrolledToBottom());
}

QTEST_MAIN(TestPersistentChatListWidget)
#include "TestPersistentChatListWidget.moc"
