#include "shared/auth_token.h"
#include "shared/protocol.h"

#include <QJsonDocument>
#include <QtTest>

class ProtocolTests final : public QObject {
	Q_OBJECT

private slots:
	void encodesAndParsesVersionedMessages()
	{
		const QByteArray frame = gtd::encodeMessage(gtd::makeMessage(
			QStringLiteral("Ping"), {{QStringLiteral("nonce"), 42}}));
		QVERIFY(frame.endsWith('\n'));
		QJsonObject message;
		QString error;
		QVERIFY2(gtd::parseMessage(frame.trimmed(), &message, &error), qPrintable(error));
		QCOMPARE(message.value(QStringLiteral("type")).toString(), QStringLiteral("Ping"));
		QCOMPARE(message.value(QStringLiteral("version")).toInt(), gtd::ProtocolVersion);
		QCOMPARE(message.value(QStringLiteral("nonce")).toInt(), 42);
	}

	void buffersPartialFrames()
	{
		QByteArray pending;
		QString error;
		const auto first = gtd::takeMessages(&pending,
			QByteArrayLiteral("{\"version\":1,\"type\":\"First\"}\n{\"version\":1,"), &error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(first.size(), 1);
		QCOMPARE(first.first().value(QStringLiteral("type")).toString(), QStringLiteral("First"));
		const auto second = gtd::takeMessages(&pending,
			QByteArrayLiteral("\"type\":\"Second\"}\n"), &error);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(second.size(), 1);
		QCOMPARE(second.first().value(QStringLiteral("type")).toString(), QStringLiteral("Second"));
		QVERIFY(pending.isEmpty());
	}

	void rejectsMalformedOrUnsupportedMessages()
	{
		QJsonObject message;
		QString error;
		QVERIFY(!gtd::parseMessage(QByteArrayLiteral("not-json"), &message, &error));
		QVERIFY(!error.isEmpty());
		QVERIFY(!gtd::parseMessage(QByteArrayLiteral("{\"version\":99,\"type\":\"Ping\"}"),
								   &message, &error));
		QVERIFY(!gtd::parseMessage(QByteArrayLiteral("{\"version\":1}"), &message, &error));
	}

	void signsAndExpiresSessionTokens()
	{
		const qint64 now = 1'800'000'000;
		const QString secret = QStringLiteral("a-long-development-secret-for-unit-tests");
		const QString token = gtd::createSessionToken(QStringLiteral("player"),
			QStringLiteral("Player One"), secret, now + 60);
		QString username;
		QString displayName;
		QVERIFY(gtd::verifySessionToken(token, secret, &username, now, &displayName));
		QCOMPARE(username, QStringLiteral("player"));
		QCOMPARE(displayName, QStringLiteral("Player One"));
		QVERIFY(!gtd::verifySessionToken(token, secret + QStringLiteral("wrong"), &username, now));
		QVERIFY(!gtd::verifySessionToken(token, secret, &username, now + 60));

		QString tampered = token;
		const QChar last = tampered.back();
		tampered[tampered.size() - 1] = last == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
		QVERIFY(!gtd::verifySessionToken(tampered, secret, &username, now));
	}
};

QTEST_GUILESS_MAIN(ProtocolTests)
#include "test_protocol.moc"
