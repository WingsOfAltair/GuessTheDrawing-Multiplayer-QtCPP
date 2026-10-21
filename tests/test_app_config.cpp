#include "shared/app_config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class AppConfigTests : public QObject {
	Q_OBJECT

private slots:
	void createsAndPersistsConfiguration();
	void mergesMissingValues();
	void rejectsMalformedFileWithoutChangingIt();
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
