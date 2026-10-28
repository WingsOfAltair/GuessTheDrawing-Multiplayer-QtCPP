#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace gtd {

class UpdateManager final : public QObject {
	Q_OBJECT

public:
	explicit UpdateManager(QObject* parent = nullptr);

	void checkForUpdates();
	bool installAndRestart(const QString& archivePath, QString* error = nullptr) const;

signals:
	void updateStatusChanged(const QString& status);
	void downloadProgressChanged(qint64 received, qint64 total);
	void updateAvailable(const QString& version, const QString& archivePath);
	void checkFailed(const QString& message);

private:
	void downloadPackage(const QString& version, const QByteArray& expectedSha256);

	QNetworkAccessManager* network_ = nullptr;
	bool checking_ = false;
	bool updateReady_ = false;
};

}
