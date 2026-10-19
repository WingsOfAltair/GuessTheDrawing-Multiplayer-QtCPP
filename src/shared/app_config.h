#pragma once

#include <QJsonObject>
#include <QString>

namespace gtd {

class AppConfig {
public:
	static QString defaultPath();
	static QString defaultExportDirectory();
	static QString defaultDictionaryPath();

	bool loadOrCreate(const QString& filePath, QString* error = nullptr);
	QString stringValue(const QString& section, const QString& key) const;
	bool setStringValue(const QString& section, const QString& key, const QString& value,
		QString* error = nullptr);
	int intValue(const QString& section, const QString& key) const;
	QString resolvedPath(const QString& section, const QString& key) const;
	QString filePath() const;

private:
	static QJsonObject defaults();
	static QJsonObject mergeDefaults(const QJsonObject& defaults, const QJsonObject& existing);
	static QString createSecret();
	bool validate(QString* error) const;
	bool save(QString* error) const;

	QJsonObject values_;
	QString filePath_;
};

}
