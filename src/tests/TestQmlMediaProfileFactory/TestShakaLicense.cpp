// Copyright The Mumble Developers. All rights reserved.

#include "License.h"

#include <QtTest/QtTest>

#include <algorithm>

class TestShakaLicense final : public QObject {
	Q_OBJECT

private slots:
	void bundledLicenseIsPublished();
};

void TestShakaLicense::bundledLicenseIsPublished() {
	const auto licenses = License::thirdPartyLicenses();
	const auto shaka = std::find_if(licenses.cbegin(), licenses.cend(), [](const LicenseInfo &license) {
		return license.name == QLatin1String("Shaka Player");
	});
	QVERIFY(shaka != licenses.cend());
	QCOMPARE(shaka->url, QStringLiteral("https://github.com/shaka-project/shaka-player"));
	QVERIFY(shaka->license.contains(QStringLiteral("Apache License")));
	QVERIFY(shaka->license.contains(QStringLiteral("Version 2.0")));

	const QString printable = License::printableThirdPartyLicenseInfo();
	QVERIFY(printable.contains(QStringLiteral("Shaka Player (https://github.com/shaka-project/shaka-player)")));
}

QTEST_GUILESS_MAIN(TestShakaLicense)

#include "TestShakaLicense.moc"
