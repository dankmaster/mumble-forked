// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "QmlAccessibilitySnapshot.h"

#include <QtCore/QMetaEnum>
#include <QtCore/QSet>
#include <QtCore/QVariantList>
#include <QtGui/QAccessible>
#include <QtGui/QWindow>

#include <algorithm>
#include <tuple>
#include <utility>

namespace {

QVariantMap errorResult(const QString &error) {
	return { { QStringLiteral("ok"), false }, { QStringLiteral("error"), error } };
}

#if QT_CONFIG(accessibility)

struct TraversalContext {
	QmlAccessibilitySnapshotLimits limits;
	QSet< quintptr > visited;
	QPoint rootOrigin;
	int nodeCount = 0;
	bool truncated = false;
};

QString boundedString(const QString &value, TraversalContext &context) {
	if (value.size() <= context.limits.maximumStringLength) {
		return value;
	}
	context.truncated = true;
	return value.left(context.limits.maximumStringLength);
}

QString roleName(const QAccessible::Role role) {
	const QMetaEnum roleEnum = QMetaEnum::fromType< QAccessible::Role >();
	const char *key = roleEnum.valueToKey(static_cast< int >(role));
	return key ? QString::fromLatin1(key) : QStringLiteral("Role%1").arg(static_cast< int >(role));
}

QVariantList stateNames(const QAccessible::State &state, const QAccessible::Role role) {
	QVariantList states;
#define APPEND_ACCESSIBLE_STATE(member) \
	if (state.member) states.push_back(QStringLiteral(#member))
	APPEND_ACCESSIBLE_STATE(active);
	APPEND_ACCESSIBLE_STATE(animated);
	APPEND_ACCESSIBLE_STATE(busy);
	APPEND_ACCESSIBLE_STATE(checkable);
	APPEND_ACCESSIBLE_STATE(checked);
	APPEND_ACCESSIBLE_STATE(checkStateMixed);
	APPEND_ACCESSIBLE_STATE(collapsed);
	APPEND_ACCESSIBLE_STATE(defaultButton);
	APPEND_ACCESSIBLE_STATE(disabled);
	APPEND_ACCESSIBLE_STATE(editable);
	APPEND_ACCESSIBLE_STATE(expandable);
	APPEND_ACCESSIBLE_STATE(expanded);
	APPEND_ACCESSIBLE_STATE(extSelectable);
	// Qt Quick may report passive labels as focusable even though they cannot
	// participate in keyboard navigation. Keep the snapshot semantic so visual
	// gates do not mistake screen-reader text for an interactive focus target.
	if (state.focusable && role != QAccessible::StaticText) states.push_back(QStringLiteral("focusable"));
	APPEND_ACCESSIBLE_STATE(focused);
	APPEND_ACCESSIBLE_STATE(hasPopup);
	APPEND_ACCESSIBLE_STATE(hotTracked);
	APPEND_ACCESSIBLE_STATE(invalid);
	APPEND_ACCESSIBLE_STATE(invisible);
	APPEND_ACCESSIBLE_STATE(linked);
	APPEND_ACCESSIBLE_STATE(marqueed);
	APPEND_ACCESSIBLE_STATE(modal);
	APPEND_ACCESSIBLE_STATE(movable);
	APPEND_ACCESSIBLE_STATE(multiLine);
	APPEND_ACCESSIBLE_STATE(multiSelectable);
	APPEND_ACCESSIBLE_STATE(offscreen);
	APPEND_ACCESSIBLE_STATE(passwordEdit);
	APPEND_ACCESSIBLE_STATE(pressed);
	APPEND_ACCESSIBLE_STATE(readOnly);
	APPEND_ACCESSIBLE_STATE(searchEdit);
	APPEND_ACCESSIBLE_STATE(selectable);
	APPEND_ACCESSIBLE_STATE(selectableText);
	APPEND_ACCESSIBLE_STATE(selected);
	APPEND_ACCESSIBLE_STATE(selfVoicing);
	APPEND_ACCESSIBLE_STATE(sizeable);
	APPEND_ACCESSIBLE_STATE(supportsAutoCompletion);
	APPEND_ACCESSIBLE_STATE(traversed);
#undef APPEND_ACCESSIBLE_STATE
	return states;
}

QVariantMap rectMap(const QRect &rect) {
	return { { QStringLiteral("x"), rect.x() }, { QStringLiteral("y"), rect.y() },
			 { QStringLiteral("width"), rect.width() }, { QStringLiteral("height"), rect.height() } };
}

QRect normalizedRect(const QRect &rect, const TraversalContext &context) {
	return rect.translated(-context.rootOrigin);
}

quintptr identityFor(QAccessibleInterface *interface) {
	if (QObject *object = interface->object()) {
		return reinterpret_cast< quintptr >(object);
	}
	return reinterpret_cast< quintptr >(interface);
}

struct ChildEntry {
	QAccessibleInterface *interface = nullptr;
	int sourceIndex = 0;
	QRect rect;
	int role = 0;
	QString name;
	QString description;
};

QVariantMap serializeNode(QAccessibleInterface *interface, const int depth, TraversalContext &context) {
	if (!interface || !interface->isValid()) {
		return { { QStringLiteral("invalid"), true } };
	}

	const quintptr identity = identityFor(interface);
	if (context.visited.contains(identity)) {
		context.truncated = true;
		return { { QStringLiteral("role"), roleName(interface->role()) },
				 { QStringLiteral("name"), boundedString(interface->text(QAccessible::Name), context) },
				 { QStringLiteral("cycle"), true } };
	}
	if (context.nodeCount >= context.limits.maximumNodes) {
		context.truncated = true;
		return { { QStringLiteral("truncated"), true } };
	}

	context.visited.insert(identity);
	++context.nodeCount;
	const QAccessible::Role role = interface->role();
	QVariantMap node {
		{ QStringLiteral("role"), roleName(role) },
		{ QStringLiteral("name"), boundedString(interface->text(QAccessible::Name), context) },
		{ QStringLiteral("description"), boundedString(interface->text(QAccessible::Description), context) },
		{ QStringLiteral("states"), stateNames(interface->state(), role) },
		{ QStringLiteral("rect"), rectMap(normalizedRect(interface->rect(), context)) },
	};
	// A button is the semantic control. Qt Quick may expose its decorative
	// contentItem/background as generic Client descendants depending on pointer
	// position and style incubation order. Those nodes carry no independent
	// action or label and made otherwise identical visual-gate captures differ.
	// Serialize buttons as leaves while retaining their complete role, state,
	// label, description and bounds.
	if (role == QAccessible::PushButton) {
		node.insert(QStringLiteral("children"), QVariantList());
		return node;
	}

	if (depth >= context.limits.maximumDepth) {
		if (interface->childCount() > 0) {
			context.truncated = true;
			node.insert(QStringLiteral("childrenTruncated"), true);
		}
		node.insert(QStringLiteral("children"), QVariantList());
		return node;
	}

	QList< ChildEntry > children;
	children.reserve(std::min(interface->childCount(), context.limits.maximumChildrenPerNode));
	for (int index = 0; index < interface->childCount(); ++index) {
		QAccessibleInterface *child = interface->child(index);
		if (!child || !child->isValid()) {
			continue;
		}
		const QAccessible::State childState = child->state();
		if (childState.invisible || childState.offscreen) {
			continue;
		}
		// Qt Quick ListView keeps released delegates in its reuse pool. Their
		// accessibility interfaces can remain valid and claim to be visible even
		// though they no longer have scene geometry. Never expose these stale,
		// non-materialized nodes to automation or assistive technology snapshots.
		const QRect childRect = child->rect();
		if (childRect.isNull() || childRect.isEmpty()) {
			continue;
		}
		if (children.size() >= context.limits.maximumChildrenPerNode) {
			context.truncated = true;
			node.insert(QStringLiteral("childrenTruncated"), true);
			break;
		}
		children.push_back({ child, index, normalizedRect(childRect, context), static_cast< int >(child->role()),
							 boundedString(child->text(QAccessible::Name), context),
							 boundedString(child->text(QAccessible::Description), context) });
	}
	std::stable_sort(children.begin(), children.end(), [](const ChildEntry &left, const ChildEntry &right) {
		return std::make_tuple(left.rect.y(), left.rect.x(), left.rect.height(), left.rect.width(), left.role, left.name,
							   left.description, left.sourceIndex)
			 < std::make_tuple(right.rect.y(), right.rect.x(), right.rect.height(), right.rect.width(), right.role,
								  right.name, right.description, right.sourceIndex);
	});

	QVariantList serializedChildren;
	serializedChildren.reserve(children.size());
	for (const ChildEntry &child : std::as_const(children)) {
		if (context.nodeCount >= context.limits.maximumNodes) {
			context.truncated = true;
			node.insert(QStringLiteral("childrenTruncated"), true);
			break;
		}
		serializedChildren.push_back(serializeNode(child.interface, depth + 1, context));
	}
	node.insert(QStringLiteral("children"), serializedChildren);
	return node;
}

#endif // QT_CONFIG(accessibility)

} // namespace

QVariantMap QmlAccessibilitySnapshot::serialize(QWindow *window, const QmlAccessibilitySnapshotLimits &limits) {
	if (!window) {
		return errorResult(QStringLiteral("No QWindow was provided for accessibility serialization."));
	}
#if QT_CONFIG(accessibility)
	QAccessibleInterface *root = QAccessible::queryAccessibleInterface(window);
	if (!root) {
		return errorResult(QStringLiteral("Qt accessibility interface is unavailable for the requested window."));
	}
	return serialize(root, limits);
#else
	Q_UNUSED(limits);
	return errorResult(QStringLiteral("Qt accessibility support is not available in this build."));
#endif
}

QVariantMap QmlAccessibilitySnapshot::serialize(QAccessibleInterface *root,
												 const QmlAccessibilitySnapshotLimits &limits) {
#if QT_CONFIG(accessibility)
	if (!root) {
		return errorResult(QStringLiteral("No QAccessibleInterface was provided for accessibility serialization."));
	}
	if (!root->isValid()) {
		return errorResult(QStringLiteral("The root Qt accessibility interface is invalid."));
	}
	if (limits.maximumDepth < 0 || limits.maximumNodes < 1 || limits.maximumChildrenPerNode < 0
		|| limits.maximumStringLength < 0) {
		return errorResult(QStringLiteral("Accessibility snapshot limits are invalid."));
	}

	TraversalContext context { limits, {}, root->rect().topLeft() };
	const QVariantMap tree = serializeNode(root, 0, context);
	return { { QStringLiteral("ok"), true }, { QStringLiteral("tree"), tree },
			 { QStringLiteral("nodeCount"), context.nodeCount }, { QStringLiteral("truncated"), context.truncated } };
#else
	Q_UNUSED(root);
	Q_UNUSED(limits);
	return errorResult(QStringLiteral("Qt accessibility support is not available in this build."));
#endif
}
