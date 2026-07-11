// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.
#include "SearchTypes.h"
#include <QtCore/QObject>
QString Search::SearchDialog::toString(const UserAction action) {
	return action == UserAction::JOIN ? QObject::tr("Join") : QObject::tr("None");
}
QString Search::SearchDialog::toString(const ChannelAction action) {
	return action == ChannelAction::JOIN ? QObject::tr("Join") : QObject::tr("None");
}
