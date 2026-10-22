#include "shared/app_config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <initializer_list>
#include <utility>

class AppConfigTests : public QObject {
	Q_OBJECT

private slots:
	void createsAndPersistsConfiguration();
	void mergesMissingValues();
	void rejectsMalformedFileWithoutChangingIt();
	void rejectsInvalidSuperAdminRefreshIntervals();
	void migratesLegacyAutomaticBanHours();
	void validatesAutomaticBanDuration();
};

void AppConfigTests::createsAndPersistsConfiguration()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("gtd-config.json"));

	gtd::AppConfig first;
	QString error;
	QVERIFY2(first.loadOrCreate(path, &error), qPrintable(error));
	const QString serverSecret = first.stringValue(QStringLiteral("security"), QStringLiteral("serverSecret"));
	const QString sessionSecret = first.stringValue(QStringLiteral("security"), QStringLiteral("sessionSecret"));
	QCOMPARE(serverSecret.size(), 64);
	QCOMPARE(sessionSecret.size(), 64);
	QVERIFY(serverSecret != sessionSecret);
	QVERIFY(QFile::exists(path));
	QCOMPARE(first.stringValue(QStringLiteral("client"), QStringLiteral("theme")), QStringLiteral("system"));
	QCOMPARE(first.stringValue(QStringLiteral("client"), QStringLiteral("exportDirectory")),
		gtd::AppConfig::defaultExportDirectory());
	QCOMPARE(first.stringValue(QStringLiteral("client"), QStringLiteral("dictionaryPath")),
		gtd::AppConfig::defaultDictionaryPath());
	QCOMPARE(first.intValue(QStringLiteral("gameServer"), QStringLiteral("intermissionSeconds")), 15);
	QCOMPARE(first.intValue(QStringLiteral("gameServer"), QStringLiteral("configRefreshSeconds")), 5);
	QCOMPARE(first.intValue(QStringLiteral("master"), QStringLiteral("automaticBanDistinctReports")), 3);
	QCOMPARE(first.intValue(QStringLiteral("master"), QStringLiteral("automaticBanDurationValue")), 1);
	QCOMPARE(first.stringValue(QStringLiteral("master"), QStringLiteral("automaticBanDurationUnit")),
		QStringLiteral("hours"));
	QVERIFY(first.stringValue(QStringLiteral("gameServer"), QStringLiteral("profanityDictionaryPath"))
		.endsWith(QStringLiteral("profanity-dictionary.txt")));
	QCOMPARE(first.intValue(QStringLiteral("master"), QStringLiteral("superAdminRefreshSeconds")), 1);

	gtd::AppConfig second;
	QVERIFY2(second.loadOrCreate(path, &error), qPrintable(error));
	QCOMPARE(second.stringValue(QStringLiteral("security"), QStringLiteral("serverSecret")), serverSecret);
	QCOMPARE(second.stringValue(QStringLiteral("security"), QStringLiteral("sessionSecret")), sessionSecret);
	QVERIFY2(second.setStringValue(QStringLiteral("client"), QStringLiteral("language"), QStringLiteral("ar"), &error),
		qPrintable(error));
	QVERIFY2(second.setStringValue(QStringLiteral("client"), QStringLiteral("theme"), QStringLiteral("dark"), &error),
		qPrintable(error));
	const QString exportDirectory = directory.filePath(QStringLiteral("custom-drawings"));
	QVERIFY2(second.setStringValue(QStringLiteral("client"), QStringLiteral("exportDirectory"), exportDirectory, &error),
		qPrintable(error));
	const QString dictionaryPath = directory.filePath(QStringLiteral("custom-dictionary.txt"));
	QVERIFY2(second.setStringValue(QStringLiteral("client"), QStringLiteral("dictionaryPath"), dictionaryPath, &error),
		qPrintable(error));
	gtd::AppConfig third;
	QVERIFY2(third.loadOrCreate(path, &error), qPrintable(error));
	QCOMPARE(third.stringValue(QStringLiteral("client"), QStringLiteral("language")), QStringLiteral("ar"));
	QCOMPARE(third.stringValue(QStringLiteral("client"), QStringLiteral("theme")), QStringLiteral("dark"));
	QCOMPARE(third.stringValue(QStringLiteral("client"), QStringLiteral("exportDirectory")), exportDirectory);
	QCOMPARE(third.stringValue(QStringLiteral("client"), QStringLiteral("dictionaryPath")), dictionaryPath);
	QCOMPARE(second.resolvedPath(QStringLiteral("master"), QStringLiteral("database")),
			 directory.filePath(QStringLiteral("gtd.sqlite")));
	QFile configuredFile(path);
	QVERIFY(configuredFile.open(QIODevice::ReadOnly));
	QJsonObject configuredRoot = QJsonDocument::fromJson(configuredFile.readAll()).object();
	configuredFile.close();
	QJsonObject master = configuredRoot.value(QStringLiteral("master")).toObject();
	master.insert(QStringLiteral("superAdminRefreshSeconds"), 7);
	configuredRoot.insert(QStringLiteral("master"), master);
	QVERIFY(configuredFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(configuredFile.write(QJsonDocument(configuredRoot).toJson()), qint64(QJsonDocument(configuredRoot).toJson().size()));
	configuredFile.close();
	gtd::AppConfig configured;
	QVERIFY2(configured.loadOrCreate(path, &error), qPrintable(error));
	QCOMPARE(configured.intValue(QStringLiteral("master"), QStringLiteral("superAdminRefreshSeconds")), 7);
}

void AppConfigTests::migratesLegacyAutomaticBanHours()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("legacy.json"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(QJsonDocument(QJsonObject{
		{QStringLiteral("version"), 1},
		{QStringLiteral("master"), QJsonObject{{QStringLiteral("automaticBanHours"), 12}}}
	}).toJson());
	file.close();

	gtd::AppConfig config;
	QString error;
	QVERIFY2(config.loadOrCreate(path, &error), qPrintable(error));
	QCOMPARE(config.intValue(QStringLiteral("master"), QStringLiteral("automaticBanDurationValue")), 12);
	QCOMPARE(config.stringValue(QStringLiteral("master"), QStringLiteral("automaticBanDurationUnit")),
		QStringLiteral("hours"));
}

void AppConfigTests::validatesAutomaticBanDuration()
{
	for (const auto& [value, unit] : {std::pair{1, QStringLiteral("seconds")},
		std::pair{5, QStringLiteral("minutes")}, std::pair{24, QStringLiteral("hours")},
		std::pair{30, QStringLiteral("days")}, std::pair{4, QStringLiteral("weeks")},
		std::pair{1, QStringLiteral("months")}, std::pair{1, QStringLiteral("years")}}) {
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("duration.json"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QJsonDocument(QJsonObject{
			{QStringLiteral("version"), 1},
			{QStringLiteral("master"), QJsonObject{
				{QStringLiteral("automaticBanDurationValue"), value},
				{QStringLiteral("automaticBanDurationUnit"), unit}}}
		}).toJson());
		file.close();
		gtd::AppConfig config;
		QString error;
		QVERIFY2(config.loadOrCreate(path, &error), qPrintable(error));
	}
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("invalid-duration.json"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(QJsonDocument(QJsonObject{
		{QStringLiteral("version"), 1},
		{QStringLiteral("master"), QJsonObject{
			{QStringLiteral("automaticBanDurationValue"), 1},
			{QStringLiteral("automaticBanDurationUnit"), QStringLiteral("fortnights")}}}
	}).toJson());
	file.close();
	gtd::AppConfig config;
	QString error;
	QVERIFY(!config.loadOrCreate(path, &error));
	QVERIFY(error.contains(QStringLiteral("automaticBanDuration")));
}

void AppConfigTests::mergesMissingValues()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("partial.json"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(QJsonDocument(QJsonObject{
		{QStringLiteral("version"), 1},
		{QStringLiteral("master"), QJsonObject{{QStringLiteral("port"), 45678}}}
	}).toJson());
	file.close();

	gtd::AppConfig config;
	QString error;
	QVERIFY2(config.loadOrCreate(path, &error), qPrintable(error));
	QCOMPARE(config.intValue(QStringLiteral("master"), QStringLiteral("port")), 45678);
	QCOMPARE(config.stringValue(QStringLiteral("master"), QStringLiteral("address")), QStringLiteral("127.0.0.1"));
	QVERIFY(config.stringValue(QStringLiteral("security"), QStringLiteral("serverSecret")).size() >= 32);

	QFile saved(path);
	QVERIFY(saved.open(QIODevice::ReadOnly));
	const QJsonObject root = QJsonDocument::fromJson(saved.readAll()).object();
	QVERIFY(root.contains(QStringLiteral("client")));
	QVERIFY(root.value(QStringLiteral("gameServer")).toObject().contains(QStringLiteral("roomId")));
}

void AppConfigTests::rejectsInvalidSuperAdminRefreshIntervals()
{
	for (const int interval : {0, 3601}) {
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("invalid-interval.json"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QJsonDocument(QJsonObject{
			{QStringLiteral("version"), 1},
			{QStringLiteral("master"), QJsonObject{{QStringLiteral("superAdminRefreshSeconds"), interval}}}
		}).toJson());
		file.close();
		gtd::AppConfig config;
		QString error;
		QVERIFY(!config.loadOrCreate(path, &error));
		QVERIFY(error.contains(QStringLiteral("superAdminRefreshSeconds")));
	}
}

void AppConfigTests::rejectsMalformedFileWithoutChangingIt()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("broken.json"));
	const QByteArray contents("not-json");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(contents), contents.size());
	file.close();

	gtd::AppConfig config;
	QString error;
	QVERIFY(!config.loadOrCreate(path, &error));
	QVERIFY(!error.isEmpty());

	QFile unchanged(path);
	QVERIFY(unchanged.open(QIODevice::ReadOnly));
	QCOMPARE(unchanged.readAll(), contents);
}

QTEST_GUILESS_MAIN(AppConfigTests)
#include "test_app_config.moc"
