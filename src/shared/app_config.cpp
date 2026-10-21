#include "shared/app_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSaveFile>

namespace gtd {
namespace {

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

bool requireString(const QJsonObject& object, const QString& key, QString* error)
{
	if (!object.value(key).isString() || object.value(key).toString().trimmed().isEmpty()) {
		setError(error, QStringLiteral("Configuration value '%1' must be a non-empty string").arg(key));
		return false;
	}
	return true;
}

bool requirePort(const QJsonObject& object, const QString& key, bool allowZero, QString* error)
{
	const QJsonValue value = object.value(key);
	if (!value.isDouble()) {
		setError(error, QStringLiteral("Configuration value '%1' must be a port number").arg(key));
		return false;
	}
	const int port = value.toInt(-1);
	if (port < (allowZero ? 0 : 1) || port > 65535) {
		setError(error, QStringLiteral("Configuration value '%1' is outside the port range").arg(key));
		return false;
	}
	return true;
}

}

QString AppConfig::defaultPath()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gtd-config.json"));
}

QString AppConfig::defaultExportDirectory()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Saved/Drawings"));
}

QString AppConfig::defaultDictionaryPath()
{
	return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dictionary"));
}

QJsonObject AppConfig::defaults()
{
	return {
		{QStringLiteral("version"), 1},
		{QStringLiteral("security"), QJsonObject{
			{QStringLiteral("serverSecret"), createSecret()},
			{QStringLiteral("sessionSecret"), createSecret()}}},
		{QStringLiteral("master"), QJsonObject{
			{QStringLiteral("address"), QStringLiteral("127.0.0.1")},
			{QStringLiteral("port"), 45450},
			{QStringLiteral("database"), QStringLiteral("gtd.sqlite")},
			{QStringLiteral("superAdminRefreshSeconds"), 1}}},
		{QStringLiteral("gameServer"), QJsonObject{
			{QStringLiteral("serverId"), QStringLiteral("local-room-1")},
			{QStringLiteral("roomId"), QStringLiteral("room-1")},
			{QStringLiteral("roomName"), QStringLiteral("Drawing Room")},
			{QStringLiteral("publicHost"), QStringLiteral("127.0.0.1")},
			{QStringLiteral("port"), 45451},
			{QStringLiteral("masterAddress"), QStringLiteral("127.0.0.1")},
			{QStringLiteral("masterPort"), 45450},
			{QStringLiteral("roundSeconds"), 60},
			{QStringLiteral("intermissionSeconds"), 15},
			{QStringLiteral("maxPlayers"), 8}}},
		{QStringLiteral("client"), QJsonObject{
			{QStringLiteral("masterAddress"), QStringLiteral("127.0.0.1")},
			{QStringLiteral("masterPort"), 45450},
			{QStringLiteral("language"), QStringLiteral("en")},
			{QStringLiteral("theme"), QStringLiteral("system")},
			{QStringLiteral("exportDirectory"), defaultExportDirectory()},
			{QStringLiteral("dictionaryPath"), defaultDictionaryPath()}}}
	};
}

QJsonObject AppConfig::mergeDefaults(const QJsonObject& defaultsObject, const QJsonObject& existing)
{
	QJsonObject merged = defaultsObject;
	for (auto iterator = existing.begin(); iterator != existing.end(); ++iterator) {
		const QJsonValue defaultValue = defaultsObject.value(iterator.key());
		if (defaultValue.isObject() && iterator.value().isObject())
			merged.insert(iterator.key(), mergeDefaults(defaultValue.toObject(), iterator.value().toObject()));
		else
			merged.insert(iterator.key(), iterator.value());
	}
	return merged;
}

QString AppConfig::createSecret()
{
	QByteArray bytes(32, Qt::Uninitialized);
	QRandomGenerator* generator = QRandomGenerator::system();
	for (qsizetype index = 0; index < bytes.size(); index += 4) {
		const quint32 value = generator->generate();
		for (qsizetype offset = 0; offset < 4; ++offset)
			bytes[index + offset] = static_cast<char>((value >> (offset * 8)) & 0xff);
	}
	return QString::fromLatin1(bytes.toHex());
}

bool AppConfig::loadOrCreate(const QString& filePath, QString* error)
{
	filePath_ = QFileInfo(filePath.isEmpty() ? defaultPath() : filePath).absoluteFilePath();
	const QFileInfo info(filePath_);
	const bool exists = info.exists();
	QJsonObject existing;
	if (exists) {
		QFile file(filePath_);
		if (!file.open(QIODevice::ReadOnly)) {
			setError(error, QStringLiteral("Cannot open configuration file: %1").arg(file.errorString()));
			return false;
		}
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			setError(error, QStringLiteral("Configuration file is invalid JSON; it was left unchanged"));
			return false;
		}
		existing = document.object();
		const QJsonValue version = existing.value(QStringLiteral("version"));
		if (!version.isUndefined() && (!version.isDouble() || version.toInt() != 1)) {
			setError(error, QStringLiteral("Unsupported configuration version"));
			return false;
		}
	}

	values_ = mergeDefaults(defaults(), existing);
	QJsonObject security = values_.value(QStringLiteral("security")).toObject();
	if (security.value(QStringLiteral("serverSecret")).toString().size() < 32)
		security.insert(QStringLiteral("serverSecret"), createSecret());
	if (security.value(QStringLiteral("sessionSecret")).toString().size() < 32)
		security.insert(QStringLiteral("sessionSecret"), createSecret());
	values_.insert(QStringLiteral("security"), security);

	if (!validate(error))
		return false;
	if (!exists || values_ != existing)
		return save(error);
	return true;
}

bool AppConfig::validate(QString* error) const
{
	const QJsonObject security = values_.value(QStringLiteral("security")).toObject();
	const QJsonObject master = values_.value(QStringLiteral("master")).toObject();
	const QJsonObject game = values_.value(QStringLiteral("gameServer")).toObject();
	const QJsonObject client = values_.value(QStringLiteral("client")).toObject();
	if (security.isEmpty() || master.isEmpty() || game.isEmpty() || client.isEmpty()) {
		setError(error, QStringLiteral("Configuration sections must be JSON objects"));
		return false;
	}
	for (const QString& key : {QStringLiteral("serverSecret"), QStringLiteral("sessionSecret")}) {
		if (!requireString(security, key, error) || security.value(key).toString().size() < 32) {
			setError(error, QStringLiteral("Security secrets must contain at least 32 characters"));
			return false;
		}
	}
	if (!requireString(master, QStringLiteral("address"), error)
		|| !requireString(master, QStringLiteral("database"), error)
		|| !requirePort(master, QStringLiteral("port"), false, error)
		|| !requireString(game, QStringLiteral("serverId"), error)
		|| !requireString(game, QStringLiteral("roomId"), error)
		|| !requireString(game, QStringLiteral("roomName"), error)
		|| !requireString(game, QStringLiteral("publicHost"), error)
		|| !requireString(game, QStringLiteral("masterAddress"), error)
		|| !requirePort(game, QStringLiteral("port"), true, error)
		|| !requirePort(game, QStringLiteral("masterPort"), false, error)
		|| !requirePort(client, QStringLiteral("masterPort"), false, error)
		|| !requireString(client, QStringLiteral("masterAddress"), error)
		|| !requireString(client, QStringLiteral("exportDirectory"), error)
		|| !requireString(client, QStringLiteral("dictionaryPath"), error))
		return false;

	const int roundSeconds = game.value(QStringLiteral("roundSeconds")).toInt(-1);
	const int intermissionSeconds = game.value(QStringLiteral("intermissionSeconds")).toInt(-1);
	const int maxPlayers = game.value(QStringLiteral("maxPlayers")).toInt(-1);
	const int superAdminRefreshSeconds = master.value(QStringLiteral("superAdminRefreshSeconds")).toInt(-1);
	if (roundSeconds < 10 || roundSeconds > 600 || intermissionSeconds < 0
		|| intermissionSeconds > 120 || maxPlayers < 2 || maxPlayers > 32) {
		setError(error, QStringLiteral("Game roundSeconds must be 10-600, intermissionSeconds 0-120, and maxPlayers 2-32"));
		return false;
	}
	if (superAdminRefreshSeconds < 1 || superAdminRefreshSeconds > 3600) {
		setError(error, QStringLiteral("Master superAdminRefreshSeconds must be 1-3600"));
		return false;
	}
	return true;
}

bool AppConfig::save(QString* error) const
{
	const QFileInfo info(filePath_);
	if (!QDir().mkpath(info.absolutePath())) {
		setError(error, QStringLiteral("Cannot create configuration directory"));
		return false;
	}
	QSaveFile file(filePath_);
	if (!file.open(QIODevice::WriteOnly)) {
		setError(error, QStringLiteral("Cannot create configuration file: %1").arg(file.errorString()));
		return false;
	}
	const QByteArray data = QJsonDocument(values_).toJson(QJsonDocument::Indented);
	if (file.write(data) != data.size() || !file.commit()) {
		setError(error, QStringLiteral("Cannot save configuration file: %1").arg(file.errorString()));
		return false;
	}
	return true;
}

QString AppConfig::stringValue(const QString& section, const QString& key) const
{
	return values_.value(section).toObject().value(key).toString();
}

bool AppConfig::setStringValue(const QString& section, const QString& key, const QString& value,
	QString* error)
{
	QJsonObject sectionValues = values_.value(section).toObject();
	const QJsonValue previousValue = sectionValues.value(key);
	sectionValues.insert(key, value);
	values_.insert(section, sectionValues);
	if (save(error))
		return true;
	if (previousValue.isUndefined())
		sectionValues.remove(key);
	else
		sectionValues.insert(key, previousValue);
	values_.insert(section, sectionValues);
	return false;
}

int AppConfig::intValue(const QString& section, const QString& key) const
{
	return values_.value(section).toObject().value(key).toInt();
}

QString AppConfig::resolvedPath(const QString& section, const QString& key) const
{
	const QString value = stringValue(section, key);
	const QFileInfo pathInfo(value);
	if (pathInfo.isAbsolute())
		return pathInfo.absoluteFilePath();
	return QDir(QFileInfo(filePath_).absolutePath()).absoluteFilePath(value);
}

QString AppConfig::filePath() const
{
	return filePath_;
}

}
