// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_USERINFORMATION_H_
#define MUMBLE_MUMBLE_USERINFORMATION_H_

#include <QtGui/QImage>
#include <QtNetwork/QSslCertificate>

#include "ui_UserInformation.h"

namespace MumbleProto {
class UserStats;
}

class QTimer;
class QLabel;

class UserInformation : public QDialog, Ui::UserInformation {
private:
	Q_OBJECT
	Q_DISABLE_COPY(UserInformation)
protected:
	bool bRequested;
	unsigned int uiSession;
	QTimer *qtTimer;
	QLabel *qlAvatar;
	QLabel *qlAvatarName;
	QLabel *qlAvatarSession;
	QImage qimgAvatar;
	QList< QSslCertificate > qlCerts;
	static QString secsToString(unsigned int secs);
	QFont qfCertificateFont;
protected slots:
	void tick();
	void on_qpbCertificate_clicked();
	void openAvatarViewer();

public:
	UserInformation(const MumbleProto::UserStats &msg, QWidget *p = nullptr);
	void update(const MumbleProto::UserStats &msg);
	void refreshAvatar();
	unsigned int session() const;
};

#endif
