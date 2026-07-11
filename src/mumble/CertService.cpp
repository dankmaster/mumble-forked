// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "CertService.h"

#include "SelfSignedCertificate.h"

#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslKey>

#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>

#define SSL_STRING(x) QString::fromLatin1(x).toUtf8().data()

bool CertService::validate(const Settings::KeyPair &keyPair) {
	bool valid = !keyPair.second.isNull() && !keyPair.first.isEmpty();
	for (const QSslCertificate &certificate : keyPair.first) {
		valid = valid && !certificate.isNull();
	}
	return valid;
}

Settings::KeyPair CertService::generate(QString name, const QString &email) {
	QSslCertificate certificate;
	QSslKey key;
	SelfSignedCertificate::generateMumbleCertificate(name, email, certificate, key);
	return Settings::KeyPair(QList< QSslCertificate >{ certificate }, key);
}

Settings::KeyPair CertService::importPkcs12(QByteArray data, const QString &password) {
	X509 *x509            = nullptr;
	EVP_PKEY *privateKey  = nullptr;
	PKCS12 *pkcs12        = nullptr;
	BIO *memory           = nullptr;
	STACK_OF(X509) *chain = nullptr;
	Settings::KeyPair result;
	int parsed = 0;

	memory = BIO_new_mem_buf(data.data(), static_cast< int >(data.size()));
	pkcs12 = d2i_PKCS12_bio(memory, nullptr);
	if (pkcs12) {
		parsed = PKCS12_parse(pkcs12, nullptr, &privateKey, &x509, &chain);
		if (!privateKey && !x509 && !password.isEmpty()) {
			if (chain) {
				if (parsed) {
					sk_X509_free(chain);
				}
				chain = nullptr;
			}
			parsed = PKCS12_parse(pkcs12, password.toUtf8().constData(), &privateKey, &x509, &chain);
		}
		if (privateKey && x509 && X509_check_private_key(x509, privateKey)) {
			unsigned char *writePointer;
			QByteArray keyData(i2d_PrivateKey(privateKey, nullptr), Qt::Uninitialized);
			writePointer = reinterpret_cast< unsigned char * >(keyData.data());
			i2d_PrivateKey(privateKey, &writePointer);

			QByteArray certificateData(i2d_X509(x509, nullptr), Qt::Uninitialized);
			writePointer = reinterpret_cast< unsigned char * >(certificateData.data());
			i2d_X509(x509, &writePointer);

			QList< QSslCertificate > certificates{ QSslCertificate(certificateData, QSsl::Der) };
			if (chain) {
				for (int index = 0; index < sk_X509_num(chain); ++index) {
					X509 *chainCertificate = sk_X509_value(chain, index);
					certificateData.resize(i2d_X509(chainCertificate, nullptr));
					writePointer = reinterpret_cast< unsigned char * >(certificateData.data());
					i2d_X509(chainCertificate, &writePointer);
					certificates.append(QSslCertificate(certificateData, QSsl::Der));
				}
			}

			Settings::KeyPair candidate(certificates, QSslKey(keyData, QSsl::Rsa, QSsl::Der));
			if (validate(candidate)) {
				result = candidate;
			}
		}
	}

	if (parsed) {
		if (privateKey) {
			EVP_PKEY_free(privateKey);
		}
		if (x509) {
			X509_free(x509);
		}
		if (chain) {
			sk_X509_free(chain);
		}
	}
	if (pkcs12) {
		PKCS12_free(pkcs12);
	}
	if (memory) {
		BIO_free(memory);
	}
	return result;
}

QByteArray CertService::exportPkcs12(const Settings::KeyPair &keyPair) {
	if (keyPair.first.isEmpty()) {
		return {};
	}

	X509 *x509            = nullptr;
	EVP_PKEY *privateKey  = nullptr;
	PKCS12 *pkcs12        = nullptr;
	BIO *memory           = nullptr;
	STACK_OF(X509) *chain = sk_X509_new_null();
	const unsigned char *readPointer;
	char *data = nullptr;
	QByteArray output;

	QByteArray certificateData = keyPair.first.first().toDer();
	QByteArray keyData         = keyPair.second.toDer();
	readPointer                = reinterpret_cast< const unsigned char * >(keyData.constData());
	privateKey                 = d2i_AutoPrivateKey(nullptr, &readPointer, keyData.length());
	if (privateKey) {
		readPointer = reinterpret_cast< const unsigned char * >(certificateData.constData());
		x509        = d2i_X509(nullptr, &readPointer, certificateData.length());
		if (x509 && X509_check_private_key(x509, privateKey)) {
			X509_keyid_set1(x509, nullptr, 0);
			X509_alias_set1(x509, nullptr, 0);
			for (qsizetype index = 1; index < keyPair.first.size(); ++index) {
				certificateData = keyPair.first.at(index).toDer();
				readPointer = reinterpret_cast< const unsigned char * >(certificateData.constData());
				if (X509 *certificate = d2i_X509(nullptr, &readPointer, certificateData.length())) {
					sk_X509_push(chain, certificate);
				}
			}

			pkcs12 = PKCS12_create(SSL_STRING(""), SSL_STRING("Mumble Identity"), privateKey, x509, chain, -1, -1,
								   0, 0, 0);
			if (pkcs12) {
				memory = BIO_new(BIO_s_mem());
				i2d_PKCS12_bio(memory, pkcs12);
				Q_UNUSED(BIO_flush(memory));
				const long size = BIO_get_mem_data(memory, &data);
				output          = QByteArray(data, static_cast< int >(size));
			}
		}
	}

	if (privateKey) {
		EVP_PKEY_free(privateKey);
	}
	if (x509) {
		X509_free(x509);
	}
	if (pkcs12) {
		PKCS12_free(pkcs12);
	}
	if (memory) {
		BIO_free(memory);
	}
	if (chain) {
		sk_X509_free(chain);
	}
	return output;
}

#undef SSL_STRING
