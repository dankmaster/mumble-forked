// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNPRODUCTDIALOGSTATEFACTORY_H_
#define MUMBLE_MUMBLE_MODERNPRODUCTDIALOGSTATEFACTORY_H_

#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

namespace Mumble::ModernProductDialogs {

struct CertificateSummary {
	bool installed = false;
	QString name;
	QString email;
	QString issuer;
	QString expires;
	QString fingerprint;
	QString highlightExpiry;
};

struct CertificateDialogInput {
	CertificateSummary certificate;
	QVariantMap fieldValues;
	QVariantMap errors;
	QString statusMessage;
};

struct ScreenShareEditorStateInput {
	QString channelName;
	QString channelId;
	QVariantList sources;
	QString selectedSourceId;
	QString sourceError;
	QVariantList resolutionOptions;
	QString resolutionDefault;
	QVariantList frameRateOptions;
	int frameRateDefault = 0;
	QVariantList audioOptions;
	QString audioDefault;
	QString audioNote;
	QString qualityNote;
	bool runtimeProbePending = false;
	QString runtimeError;
	QVariant sourcesLoading;
};

QVariantMap certificateDialog(const CertificateDialogInput &input);
QVariantMap screenShareEditorState(const ScreenShareEditorStateInput &input);
QVariantMap screenShareEditorDialog(const QVariantMap &state);
QVariantMap screenShareEditorDialog(const ScreenShareEditorStateInput &input);

} // namespace Mumble::ModernProductDialogs

#endif
