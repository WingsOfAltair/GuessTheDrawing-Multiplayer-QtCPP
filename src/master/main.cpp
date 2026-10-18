#include "master/master_server.h"
#include "shared/app_config.h"
#include "storage/account_store.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTextStream>

int main(int argc, char* argv[])
{
	QCoreApplication application(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("gtd_master"));
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Guess The Drawing master service"));
	parser.addHelpOption();
	parser.addOption({QStringLiteral("config"), QStringLiteral("Shared runtime configuration file"), QStringLiteral("path")});
	parser.addOption({QStringLiteral("address"), QStringLiteral("Bind address"),
					  QStringLiteral("address")});
	parser.addOption({QStringLiteral("port"), QStringLiteral("Listen port"),
					  QStringLiteral("port")});
	parser.addOption({QStringLiteral("database"), QStringLiteral("SQLite database path"),
					  QStringLiteral("path")});
	parser.addOption({QStringLiteral("server-secret"), QStringLiteral("Game-server registration secret"),
					  QStringLiteral("secret")});
	parser.addOption({QStringLiteral("session-secret"), QStringLiteral("Session signing secret"),
					  QStringLiteral("secret")});
	parser.process(application);

	gtd::AppConfig config;
	QString error;
	if (!config.loadOrCreate(parser.value(QStringLiteral("config")), &error)) {
		QTextStream(stderr) << "Configuration error: " << error << '\n';
		return 2;
	}
	const QString addressValue = parser.isSet(QStringLiteral("address"))
		? parser.value(QStringLiteral("address")) : config.stringValue(QStringLiteral("master"), QStringLiteral("address"));
	const QString portText = parser.isSet(QStringLiteral("port"))
		? parser.value(QStringLiteral("port")) : QString::number(config.intValue(QStringLiteral("master"), QStringLiteral("port")));
	const QString databasePath = parser.isSet(QStringLiteral("database"))
		? parser.value(QStringLiteral("database")) : config.resolvedPath(QStringLiteral("master"), QStringLiteral("database"));
	const QString serverSecret = parser.isSet(QStringLiteral("server-secret"))
		? parser.value(QStringLiteral("server-secret")) : qEnvironmentVariable("GTD_SERVER_SECRET", config.stringValue(QStringLiteral("security"), QStringLiteral("serverSecret")));
	const QString sessionSecret = parser.isSet(QStringLiteral("session-secret"))
		? parser.value(QStringLiteral("session-secret")) : qEnvironmentVariable("GTD_SESSION_SECRET", config.stringValue(QStringLiteral("security"), QStringLiteral("sessionSecret")));
	if (serverSecret.size() < 32 || sessionSecret.size() < 32) {
		QTextStream(stderr) << "Server and session secrets must contain at least 32 characters.\n";
		return 2;
	}

	bool portOk = false;
	const uint portValue = portText.toUInt(&portOk);
	if (!portOk || portValue > 65535) {
		QTextStream(stderr) << "Invalid master port.\n";
		return 2;
	}

	gtd::AccountStore accountStore(databasePath);
	if (!accountStore.initialize(&error)) {
		QTextStream(stderr) << "Database initialization failed: " << error << '\n';
		return 1;
	}

	gtd::MasterServer server(&accountStore, serverSecret, sessionSecret);
	const QHostAddress address(addressValue);
	if (address.isNull() || !server.start(address, static_cast<quint16>(portValue))) {
		QTextStream(stderr) << "Unable to start master on " << address.toString()
							<< ':' << portValue << '\n';
		return 1;
	}
	QTextStream(stdout) << "Master listening on " << server.serverAddress().toString()
						<< ':' << server.serverPort() << '\n';
	return application.exec();
}
