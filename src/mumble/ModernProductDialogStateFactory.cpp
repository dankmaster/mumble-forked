// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernProductDialogStateFactory.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSize>

namespace {
	// Keep these strings in MainWindow's established translation context even
	// though their frontend-neutral DTO construction now lives in this factory.
	[[maybe_unused]] constexpr const char *ProductDialogTranslations[] = {
		QT_TRANSLATE_NOOP("MainWindow", "A valid certificate is installed"),
		QT_TRANSLATE_NOOP("MainWindow", "Action"),
		QT_TRANSLATE_NOOP("MainWindow", "Apply"),
		QT_TRANSLATE_NOOP("MainWindow", "Browse"),
		QT_TRANSLATE_NOOP("MainWindow", "Cancel"),
		QT_TRANSLATE_NOOP("MainWindow", "Certificate"),
		QT_TRANSLATE_NOOP("MainWindow", "Certificate action"),
		QT_TRANSLATE_NOOP("MainWindow", "Choose one operation. The form only shows fields needed for that operation."),
		QT_TRANSLATE_NOOP("MainWindow", "Close"),
		QT_TRANSLATE_NOOP("MainWindow", "Create"),
		QT_TRANSLATE_NOOP("MainWindow", "Create with name and email"),
		QT_TRANSLATE_NOOP("MainWindow", "Creates a new client certificate using the default local identity."),
		QT_TRANSLATE_NOOP("MainWindow", "Current certificate"),
		QT_TRANSLATE_NOOP("MainWindow", "Elapsed"),
		QT_TRANSLATE_NOOP("MainWindow", "Email"),
		QT_TRANSLATE_NOOP("MainWindow", "Expires"),
		QT_TRANSLATE_NOOP("MainWindow", "Export"),
		QT_TRANSLATE_NOOP("MainWindow", "Export current certificate"),
		QT_TRANSLATE_NOOP("MainWindow", "Export file"),
		QT_TRANSLATE_NOOP("MainWindow", "Filename"),
		QT_TRANSLATE_NOOP("MainWindow", "Format"),
		QT_TRANSLATE_NOOP("MainWindow", "Idle"),
		QT_TRANSLATE_NOOP("MainWindow", "Import"),
		QT_TRANSLATE_NOOP("MainWindow", "Import file"),
		QT_TRANSLATE_NOOP("MainWindow", "Import password"),
		QT_TRANSLATE_NOOP("MainWindow", "Import PKCS#12"),
		QT_TRANSLATE_NOOP("MainWindow", "Installed"),
		QT_TRANSLATE_NOOP("MainWindow", "Issuer"),
		QT_TRANSLATE_NOOP("MainWindow", "Manage the client certificate used for account identity and server authentication."),
		QT_TRANSLATE_NOOP("MainWindow", "Missing"),
		QT_TRANSLATE_NOOP("MainWindow", "Mixdown"),
		QT_TRANSLATE_NOOP("MainWindow", "Mode"),
		QT_TRANSLATE_NOOP("MainWindow", "Multichannel"),
		QT_TRANSLATE_NOOP("MainWindow", "Multichannel + transport"),
		QT_TRANSLATE_NOOP("MainWindow", "Name"),
		QT_TRANSLATE_NOOP("MainWindow", "No valid certificate is installed"),
		QT_TRANSLATE_NOOP("MainWindow", "None"),
		QT_TRANSLATE_NOOP("MainWindow", "Optional display name stored in the certificate."),
		QT_TRANSLATE_NOOP("MainWindow", "Optional. Leave blank if you do not want an email address in the certificate."),
		QT_TRANSLATE_NOOP("MainWindow", "Output"),
		QT_TRANSLATE_NOOP("MainWindow", "Pick source"),
		QT_TRANSLATE_NOOP("MainWindow", "Quick create"),
		QT_TRANSLATE_NOOP("MainWindow", "Record the current voice session."),
		QT_TRANSLATE_NOOP("MainWindow", "Recorder"),
		QT_TRANSLATE_NOOP("MainWindow", "Recording"),
		QT_TRANSLATE_NOOP("MainWindow", "Refresh"),
		QT_TRANSLATE_NOOP("MainWindow", "Retry runtime check"),
		QT_TRANSLATE_NOOP("MainWindow", "SHA-1 fingerprint"),
		QT_TRANSLATE_NOOP("MainWindow", "Share to %1"),
		QT_TRANSLATE_NOOP("MainWindow", "Start"),
		QT_TRANSLATE_NOOP("MainWindow", "Start screen share"),
		QT_TRANSLATE_NOOP("MainWindow", "Start sharing"),
		QT_TRANSLATE_NOOP("MainWindow", "Status"),
		QT_TRANSLATE_NOOP("MainWindow", "Checking the local screen-share runtime…"),
		QT_TRANSLATE_NOOP("MainWindow", "Stop"),
		QT_TRANSLATE_NOOP("MainWindow", "Target directory"),
		QT_TRANSLATE_NOOP("MainWindow", "Transport only"),
		QT_TRANSLATE_NOOP("MainWindow", "Voice recorder")
	};

	QString productTr(const char *text) {
		return QCoreApplication::translate("MainWindow", text);
	}

	QVariantMap action(const QString &id, const QString &label, const bool enabled = true,
					   const QString &tone = QString(), const bool closesDialog = false) {
		QVariantMap value {
			{ QStringLiteral("kind"), QStringLiteral("action") },
			{ QStringLiteral("id"), id },
			{ QStringLiteral("label"), label },
			{ QStringLiteral("enabled"), enabled },
			{ QStringLiteral("checked"), false },
			{ QStringLiteral("closesDialog"), closesDialog }
		};
		if (!tone.isEmpty()) value.insert(QStringLiteral("tone"), tone);
		return value;
	}

	QVariantMap section(const QString &id, const QString &title, const QVariantList &fields,
						const QString &presentation = QString(), const QString &subtitle = QString()) {
		QVariantMap value {
			{ QStringLiteral("id"), id },
			{ QStringLiteral("title"), title },
			{ QStringLiteral("fields"), fields }
		};
		if (!presentation.isEmpty()) value.insert(QStringLiteral("presentation"), presentation);
		if (!subtitle.isEmpty()) value.insert(QStringLiteral("subtitle"), subtitle);
		return value;
	}

	QVariantMap field(const QString &id, const QString &label, const QString &type,
					  const QVariant &value = QVariant(), const bool enabled = true) {
		return {
			{ QStringLiteral("id"), id }, { QStringLiteral("label"), label },
			{ QStringLiteral("type"), type }, { QStringLiteral("value"), value },
			{ QStringLiteral("enabled"), enabled }
		};
	}

	QVariantMap readonlyField(const QString &id, const QString &label, const QVariant &value) {
		return field(id, label, QStringLiteral("readonly"), value, false);
	}

	QVariantMap noteField(const QString &text) {
		return { { QStringLiteral("type"), QStringLiteral("note") }, { QStringLiteral("text"), text } };
	}

	QVariantMap option(const QString &label, const QVariant &value, const bool enabled = true) {
		return { { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
			{ QStringLiteral("enabled"), enabled } };
	}

	QVariantMap selectField(const QString &id, const QString &label, const QVariant &value,
						const QVariantList &options, const QString &valueType = QStringLiteral("number"),
						const bool enabled = true) {
		QVariantMap result = field(id, label, QStringLiteral("select"), value, enabled);
		result.insert(QStringLiteral("options"), options);
		result.insert(QStringLiteral("valueType"), valueType);
		return result;
	}

	QVariantMap pathPickerField(const QString &id, const QString &label, const QString &value,
							const QString &browseActionId, const QString &browseLabel, const bool enabled = true) {
		QVariantMap result = field(id, label, QStringLiteral("pathPicker"), value, enabled);
		result.insert(QStringLiteral("browseActionId"), browseActionId);
		result.insert(QStringLiteral("browseLabel"), browseLabel);
		return result;
	}

	QVariantMap visibleWhen(QVariantMap value, const QString &fieldId, const QStringList &acceptedValues) {
		value.insert(QStringLiteral("visibleWhen"), QVariantMap {
			{ QStringLiteral("fieldId"), fieldId }, { QStringLiteral("values"), acceptedValues }
		});
		return value;
	}

	QVariantMap highlighted(const QString &label, const QVariant &value, const QString &tone = QString()) {
		QVariantMap result { { QStringLiteral("label"), label }, { QStringLiteral("value"), value } };
		if (!tone.isEmpty()) result.insert(QStringLiteral("tone"), tone);
		return result;
	}

	QVariantMap dialog(const QString &id, const QString &kind, const QString &title, const QString &subtitle,
					   const QVariantList &sections, const QVariantList &actions, const QString &primaryActionId,
					   const QString &initialFocusId, const QSize &size) {
		QVariantMap result {
			{ QStringLiteral("id"), id }, { QStringLiteral("kind"), kind },
			{ QStringLiteral("title"), title }, { QStringLiteral("subtitle"), subtitle },
			{ QStringLiteral("sections"), sections }, { QStringLiteral("actions"), actions },
			{ QStringLiteral("primaryActionId"), primaryActionId },
			{ QStringLiteral("initialFocusId"), initialFocusId }
		};
		if (size.isValid()) {
			result.insert(QStringLiteral("width"), size.width());
			result.insert(QStringLiteral("height"), size.height());
		}
		return result;
	}

	QVariantList certificateFields(const Mumble::ModernProductDialogs::CertificateSummary &certificate) {
		QVariantList fields {
			readonlyField(QStringLiteral("cert.current.status"), productTr("Status"),
				certificate.installed ? productTr("A valid certificate is installed")
								  : productTr("No valid certificate is installed"))
		};
		if (!certificate.installed) return fields;
		fields.push_back(readonlyField(QStringLiteral("cert.current.name"), productTr("Name"), certificate.name));
		fields.push_back(readonlyField(QStringLiteral("cert.current.email"), productTr("Email"), certificate.email));
		fields.push_back(readonlyField(QStringLiteral("cert.current.issuer"), productTr("Issuer"), certificate.issuer));
		fields.push_back(readonlyField(QStringLiteral("cert.current.expires"), productTr("Expires"), certificate.expires));
		fields.push_back(readonlyField(QStringLiteral("cert.current.fingerprint"), productTr("SHA-1 fingerprint"),
			certificate.fingerprint));
		return fields;
	}
}

namespace Mumble::ModernProductDialogs {

QVariantMap certificateDialog(const CertificateDialogInput &input) {
	const bool hasCertificate = input.certificate.installed;
	QString selectedMode = input.fieldValues.value(QStringLiteral("cert.mode")).toString();
	if (selectedMode.isEmpty()) selectedMode = hasCertificate ? QStringLiteral("export") : QStringLiteral("quick");

	const QVariantList modeOptions {
		option(productTr("Quick create"), QStringLiteral("quick")),
		option(productTr("Create with name and email"), QStringLiteral("create")),
		option(productTr("Import PKCS#12"), QStringLiteral("import")),
		option(productTr("Export current certificate"), QStringLiteral("export"), hasCertificate)
	};
	const QString selectedModeLabel = selectedMode == QLatin1String("quick") ? productTr("Quick create")
		: selectedMode == QLatin1String("create") ? productTr("Create")
		: selectedMode == QLatin1String("import") ? productTr("Import") : productTr("Export");

	QVariantMap modeField = selectField(QStringLiteral("cert.mode"), productTr("Action"), selectedMode,
		modeOptions, QStringLiteral("string"));
	modeField.insert(QStringLiteral("presentation"), QStringLiteral("operation"));
	QVariantMap nameField = field(QStringLiteral("cert.name"), productTr("Name"), QStringLiteral("text"),
		input.fieldValues.value(QStringLiteral("cert.name")).toString());
	nameField.insert(QStringLiteral("hint"), productTr("Optional display name stored in the certificate."));
	QVariantMap emailField = field(QStringLiteral("cert.email"), productTr("Email"), QStringLiteral("text"),
		input.fieldValues.value(QStringLiteral("cert.email")).toString());
	emailField.insert(QStringLiteral("hint"),
		productTr("Optional. Leave blank if you do not want an email address in the certificate."));

	QVariantList workflowFields {
		modeField,
		visibleWhen(noteField(productTr("Creates a new client certificate using the default local identity.")),
			QStringLiteral("cert.mode"), { QStringLiteral("quick") }),
		visibleWhen(nameField, QStringLiteral("cert.mode"), { QStringLiteral("create") }),
		visibleWhen(emailField, QStringLiteral("cert.mode"), { QStringLiteral("create") }),
		visibleWhen(pathPickerField(QStringLiteral("cert.importPath"), productTr("Import file"),
			input.fieldValues.value(QStringLiteral("cert.importPath")).toString(),
			QStringLiteral("browseCertificateImport"), productTr("Browse")),
			QStringLiteral("cert.mode"), { QStringLiteral("import") }),
		visibleWhen(field(QStringLiteral("cert.password"), productTr("Import password"), QStringLiteral("password"),
			input.fieldValues.value(QStringLiteral("cert.password")).toString()),
			QStringLiteral("cert.mode"), { QStringLiteral("import") }),
		visibleWhen(pathPickerField(QStringLiteral("cert.exportPath"), productTr("Export file"),
			input.fieldValues.value(QStringLiteral("cert.exportPath")).toString(),
			QStringLiteral("browseCertificateExport"), productTr("Browse")),
			QStringLiteral("cert.mode"), { QStringLiteral("export") })
	};
	if (!input.statusMessage.trimmed().isEmpty()) workflowFields.push_back(noteField(input.statusMessage));

	QVariantMap result = dialog(QStringLiteral("certificate"), QStringLiteral("certificate"), productTr("Certificate"),
		productTr("Manage the client certificate used for account identity and server authentication."),
		{ section(QStringLiteral("certificate-current"), productTr("Current certificate"),
			certificateFields(input.certificate), QStringLiteral("certificate-current")),
		  section(QStringLiteral("certificate-action"), productTr("Certificate action"), workflowFields,
			QStringLiteral("certificate-action"),
			productTr("Choose one operation. The form only shows fields needed for that operation.")) },
		{ action(QStringLiteral("cancel"), productTr("Close"), true, QString(), true),
		  action(QStringLiteral("runCertificateAction"), productTr("Apply"), true, QStringLiteral("accent")) },
		QStringLiteral("runCertificateAction"), QStringLiteral("cert.mode"), QSize(820, 680));
	result.insert(QStringLiteral("highlights"), QVariantList {
		highlighted(productTr("Status"), hasCertificate ? productTr("Installed") : productTr("Missing"),
			hasCertificate ? QStringLiteral("good") : QStringLiteral("warning")),
		highlighted(productTr("Action"), selectedModeLabel),
		highlighted(productTr("Expires"),
			hasCertificate && !input.certificate.highlightExpiry.isEmpty()
				? input.certificate.highlightExpiry : productTr("None"))
	});
	result.insert(QStringLiteral("errors"), input.errors);
	return result;
}

QVariantMap screenShareEditorState(const ScreenShareEditorStateInput &input) {
	QVariantMap result {
		{ QStringLiteral("channelName"), input.channelName },
		{ QStringLiteral("channelId"), input.channelId },
		{ QStringLiteral("sources"), input.sources },
		{ QStringLiteral("selectedSourceId"), input.selectedSourceId },
		{ QStringLiteral("resolutionOptions"), input.resolutionOptions },
		{ QStringLiteral("resolutionDefault"), input.resolutionDefault },
		{ QStringLiteral("frameRateOptions"), input.frameRateOptions },
		{ QStringLiteral("frameRateDefault"), input.frameRateDefault },
		{ QStringLiteral("audioOptions"), input.audioOptions },
		{ QStringLiteral("audioDefault"), input.audioDefault },
		{ QStringLiteral("audioNote"), input.audioNote },
		{ QStringLiteral("runtimeProbePending"), input.runtimeProbePending },
		{ QStringLiteral("runtimeError"), input.runtimeError.trimmed() }
	};
	if (!input.sourceError.trimmed().isEmpty()) {
		result.insert(QStringLiteral("sourceError"), input.sourceError.trimmed());
	}
	if (!input.qualityNote.isEmpty()) result.insert(QStringLiteral("qualityNote"), input.qualityNote);
	if (input.sourcesLoading.isValid()) result.insert(QStringLiteral("sourcesLoading"), input.sourcesLoading);
	if (input.portalCaptureAvailable) {
		result.insert(QStringLiteral("portalCaptureAvailable"), true);
		result.insert(QStringLiteral("portalSourcePicked"), input.portalSourcePicked);
		result.insert(QStringLiteral("portalSourcePicking"), input.portalSourcePicking);
		if (!input.portalSourceLabel.isEmpty())
			result.insert(QStringLiteral("portalSourceLabel"), input.portalSourceLabel);
		if (!input.portalSourceError.trimmed().isEmpty())
			result.insert(QStringLiteral("portalSourceError"), input.portalSourceError.trimmed());
	}
	return result;
}

QVariantMap screenShareEditorDialog(const QVariantMap &state) {
	const QString channelName = state.value(QStringLiteral("channelName")).toString();
	const QString selectedSourceId = state.value(QStringLiteral("selectedSourceId")).toString().trimmed();
	QString firstSourceId;
	bool selectedSourceExists = false;
	for (const QVariant &sectionValue : state.value(QStringLiteral("sources")).toList()) {
		for (const QVariant &itemValue : sectionValue.toMap().value(QStringLiteral("items")).toList()) {
			const QString sourceId = itemValue.toMap().value(QStringLiteral("id")).toString().trimmed();
			if (sourceId.isEmpty()) continue;
			if (firstSourceId.isEmpty()) firstSourceId = sourceId;
			if (sourceId == selectedSourceId) selectedSourceExists = true;
		}
	}
	const QString sourceError = state.value(QStringLiteral("sourceError")).toString().trimmed();
	const bool runtimeProbePending = state.value(QStringLiteral("runtimeProbePending")).toBool();
	const QString runtimeError = state.value(QStringLiteral("runtimeError")).toString().trimmed();
	const bool portalCaptureAvailable = state.value(QStringLiteral("portalCaptureAvailable")).toBool();
	const bool portalSourcePicked = state.value(QStringLiteral("portalSourcePicked")).toBool();
	const bool portalSourcePicking = state.value(QStringLiteral("portalSourcePicking")).toBool();
	const QString portalSourceError = state.value(QStringLiteral("portalSourceError")).toString().trimmed();

	const bool startEnabled = portalCaptureAvailable
		? (portalSourcePicked && !portalSourcePicking && !runtimeProbePending && runtimeError.isEmpty())
		: (selectedSourceExists && !runtimeProbePending && runtimeError.isEmpty());
	QVariantList actions { action(QStringLiteral("cancel"), productTr("Cancel"), true, QString(), true) };
	if (portalCaptureAvailable) {
		actions.push_back(action(QStringLiteral("screenShare.pickSource"), productTr("Pick source"),
			!portalSourcePicking && !portalSourcePicked && !runtimeProbePending && runtimeError.isEmpty(),
			QStringLiteral("secondary")));
	}
	if (!runtimeError.isEmpty()) {
		actions.push_back(action(QStringLiteral("screenShare.retryRuntime"), productTr("Retry runtime check"), true,
			QStringLiteral("accent")));
	}
	actions.push_back(action(QStringLiteral("screenShare.start"), productTr("Start sharing"), startEnabled,
		QStringLiteral("accent"), false));

	QString focusId;
	if (portalCaptureAvailable) {
		focusId = portalSourcePicked
			? QStringLiteral("dialogAction_screenShare.start")
			: (portalSourcePicking ? QStringLiteral("dialogAction_cancel")
				: QStringLiteral("dialogAction_screenShare.pickSource"));
	} else if (runtimeProbePending) {
		focusId = QStringLiteral("dialogAction_cancel");
	} else if (!runtimeError.isEmpty()) {
		focusId = QStringLiteral("dialogAction_screenShare.retryRuntime");
	} else if (selectedSourceExists) {
		focusId = QStringLiteral("screenShareSource_%1").arg(selectedSourceId);
	} else {
		focusId = QStringLiteral("dialogAction_cancel");
	}
	QVariantMap result = dialog(QStringLiteral("screenShare"), QStringLiteral("screenShare"),
		productTr("Start screen share"), productTr("Share to %1").arg(channelName), {},
		actions,
		QStringLiteral("screenShare.start"),
		focusId, QSize(720, 600));
	result.insert(QStringLiteral("tone"), QStringLiteral("wide"));
	result.insert(QStringLiteral("screenShare"), state);
	if (!sourceError.isEmpty()) {
		result.insert(QStringLiteral("errors"),
			QVariantMap { { QStringLiteral("screenShare.source"), sourceError } });
		result.insert(QStringLiteral("statusMessage"), sourceError);
		result.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	} else if (!portalSourceError.isEmpty()) {
		result.insert(QStringLiteral("errors"),
			QVariantMap { { QStringLiteral("screenShare.source"), portalSourceError } });
		result.insert(QStringLiteral("statusMessage"), portalSourceError);
		result.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	} else if (runtimeProbePending) {
		result.insert(QStringLiteral("runtimeStatus"), productTr("Checking the local screen-share runtime…"));
	} else if (!runtimeError.isEmpty()) {
		result.insert(QStringLiteral("errors"),
			QVariantMap { { QStringLiteral("screenShare.runtime"), runtimeError } });
		result.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	}
	return result;
}

QVariantMap screenShareEditorDialog(const ScreenShareEditorStateInput &input) {
	return screenShareEditorDialog(screenShareEditorState(input));
}

} // namespace Mumble::ModernProductDialogs
