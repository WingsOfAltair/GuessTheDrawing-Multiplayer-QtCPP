#include "client/main_window.h"
#include "shared/app_config.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char* argv[])
{
	QApplication application(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("gtd_client"));
	QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Guess The Drawing desktop client"));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({QStringLiteral("config"), QStringLiteral("Shared runtime configuration file"), QStringLiteral("path")});
	parser.addOption({QStringLiteral("master-address"), QStringLiteral("Master service address"),
					  QStringLiteral("host")});
	parser.addOption({QStringLiteral("master-port"), QStringLiteral("Master service port"),
					  QStringLiteral("port")});
	parser.process(application);

	gtd::AppConfig config;
	QString error;
	if (!config.loadOrCreate(parser.value(QStringLiteral("config")), &error)) {
		QTextStream(stderr) << "Configuration error: " << error << '\n';
		return 2;
	}
	const QString masterAddress = parser.isSet(QStringLiteral("master-address"))
		? parser.value(QStringLiteral("master-address")) : config.stringValue(QStringLiteral("client"), QStringLiteral("masterAddress"));
	const QString portText = parser.isSet(QStringLiteral("master-port"))
		? parser.value(QStringLiteral("master-port")) : QString::number(config.intValue(QStringLiteral("client"), QStringLiteral("masterPort")));

	bool portOk = false;
	const uint masterPort = portText.toUInt(&portOk);
	if (!portOk || masterPort == 0 || masterPort > 65535) {
		QTextStream(stderr) << "Invalid master port.\n";
		return 2;
	}

	gtd::MainWindow window(config, masterAddress,
						   static_cast<quint16>(masterPort));
	window.show();
	return application.exec();
}
