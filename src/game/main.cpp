#include "game/game_server.h"
#include "shared/app_config.h"
#include "shared/game_rules.h"
#include "shared/protocol.h"

#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QTextStream>

int main(int argc, char* argv[])
{
	QCoreApplication application(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("gtd_game_server"));
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Guess The Drawing dedicated room server"));
	parser.addHelpOption();
	parser.addOption({QStringLiteral("config"), QStringLiteral("Shared runtime configuration file"), QStringLiteral("path")});
	parser.addOption({QStringLiteral("server-id"), QStringLiteral("Unique server ID"),
					  QStringLiteral("id")});
	parser.addOption({QStringLiteral("room-id"), QStringLiteral("Room ID"),
					  QStringLiteral("id")});
	parser.addOption({QStringLiteral("room-name"), QStringLiteral("Room display name"),
					  QStringLiteral("name")});
	parser.addOption({QStringLiteral("public-host"), QStringLiteral("Advertised address for clients"),
					  QStringLiteral("host")});
	parser.addOption({QStringLiteral("port"), QStringLiteral("Room listen port; 0 chooses an ephemeral port"),
					  QStringLiteral("port")});
	parser.addOption({QStringLiteral("master-address"), QStringLiteral("Master service address"),
					  QStringLiteral("host")});
	parser.addOption({QStringLiteral("master-port"), QStringLiteral("Master service port"),
					  QStringLiteral("port")});
	parser.addOption({QStringLiteral("round-seconds"), QStringLiteral("Seconds per drawing round (10-600)"),
					  QStringLiteral("seconds")});
	parser.addOption({QStringLiteral("intermission-seconds"), QStringLiteral("Seconds before each drawing round (0 disables; maximum 120)"),
					  QStringLiteral("seconds")});
	parser.addOption({QStringLiteral("max-players"), QStringLiteral("Maximum room players (2-32)"),
					  QStringLiteral("count")});
	parser.addOption({QStringLiteral("server-secret"), QStringLiteral("Master registration secret"),
					  QStringLiteral("secret")});
	parser.addOption({QStringLiteral("session-secret"), QStringLiteral("Session token verification secret"),
				  QStringLiteral("secret")});
	parser.process(application);

	gtd::AppConfig config;
	QString error;
	if (!config.loadOrCreate(parser.value(QStringLiteral("config")), &error)) {
		QTextStream(stderr) << "Configuration error: " << error << '\n';
		return 2;
	}
	const auto configuredString = [&parser, &config](const QString& option, const QString& key) {
		return parser.isSet(option) ? parser.value(option) : config.stringValue(QStringLiteral("gameServer"), key);
	};
	const auto configuredInt = [&parser, &config](const QString& option, const QString& key) {
		return parser.isSet(option) ? parser.value(option).toInt() : config.intValue(QStringLiteral("gameServer"), key);
	};
	const QString serverId = configuredString(QStringLiteral("server-id"), QStringLiteral("serverId"));
	const QString roomId = configuredString(QStringLiteral("room-id"), QStringLiteral("roomId"));
	const QString roomName = configuredString(QStringLiteral("room-name"), QStringLiteral("roomName"));
	const QString publicHost = configuredString(QStringLiteral("public-host"), QStringLiteral("publicHost"));
	const QString masterAddress = configuredString(QStringLiteral("master-address"), QStringLiteral("masterAddress"));
	const QString serverSecret = parser.isSet(QStringLiteral("server-secret"))
		? parser.value(QStringLiteral("server-secret")) : qEnvironmentVariable("GTD_SERVER_SECRET", config.stringValue(QStringLiteral("security"), QStringLiteral("serverSecret")));
	const QString sessionSecret = parser.isSet(QStringLiteral("session-secret"))
		? parser.value(QStringLiteral("session-secret")) : qEnvironmentVariable("GTD_SESSION_SECRET", config.stringValue(QStringLiteral("security"), QStringLiteral("sessionSecret")));
	const QString roomPasskey = qEnvironmentVariable("GTD_ROOM_PASSKEY",
		config.stringValue(QStringLiteral("gameServer"), QStringLiteral("roomPasskey")));

	bool portOk = false;
	bool masterPortOk = false;
	bool durationOk = false;
	bool maxPlayersOk = false;
	const uint roomPort = static_cast<uint>(configuredInt(QStringLiteral("port"), QStringLiteral("port")));
	const uint masterPort = static_cast<uint>(configuredInt(QStringLiteral("master-port"), QStringLiteral("masterPort")));
	const int duration = configuredInt(QStringLiteral("round-seconds"), QStringLiteral("roundSeconds"));
	const int intermissionDuration = configuredInt(QStringLiteral("intermission-seconds"), QStringLiteral("intermissionSeconds"));
	const int maxPlayers = configuredInt(QStringLiteral("max-players"), QStringLiteral("maxPlayers"));
	portOk = roomPort <= 65535;
	masterPortOk = masterPort <= 65535;
	durationOk = duration >= 10 && duration <= 600;
	const bool intermissionOk = intermissionDuration >= 0 && intermissionDuration <= 120;
	maxPlayersOk = maxPlayers >= 2 && maxPlayers <= 32;
	if (!portOk || roomPort > 65535 || !masterPortOk || masterPort == 0 || masterPort > 65535
		|| !durationOk || !intermissionOk || !maxPlayersOk || serverSecret.size() < 32 || sessionSecret.size() < 32
		|| roomPasskey.size() > gtd::MaximumRoomPasskeyLength
		|| (!roomPasskey.isEmpty() && roomPasskey.size() < 8)) {
		QTextStream(stderr) << "Invalid port/settings or security secrets shorter than 32 characters.\n";
		return 2;
	}

	gtd::RoomInfo room;
	room.roomId = roomId;
	room.roomName = roomName;
	room.host = publicHost;
	room.port = static_cast<quint16>(roomPort);
	room.maxPlayers = qBound(2, maxPlayers, 32);
	room.roundSeconds = gtd::clampRoundSeconds(duration);
	QByteArray roomPasskeySalt;
	QByteArray roomPasskeyHash;
	if (!roomPasskey.isEmpty()) {
		roomPasskeySalt.resize(32);
		for (qsizetype index = 0; index < roomPasskeySalt.size(); index += 8) {
			const quint64 random = QRandomGenerator::system()->generate64();
			for (qsizetype offset = 0; offset < 8; ++offset)
				roomPasskeySalt[index + offset] = static_cast<char>((random >> (offset * 8)) & 0xff);
		}
		roomPasskeyHash = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
			roomPasskey.toUtf8(), roomPasskeySalt, 210000, 32);
	}

	gtd::GameServer server(room, serverId,
						   publicHost,
						   masterAddress,
						   static_cast<quint16>(masterPort), serverSecret, sessionSecret,
						   config.stringValue(QStringLiteral("client"), QStringLiteral("dictionaryPath")),
						   intermissionDuration, nullptr, roomPasskeySalt, roomPasskeyHash,
						   config.resolvedPath(QStringLiteral("gameServer"), QStringLiteral("profanityDictionaryPath")),
						   config.filePath(), config.intValue(QStringLiteral("gameServer"),
							QStringLiteral("configRefreshSeconds")), config.intValue(QStringLiteral("gameServer"),
							QStringLiteral("reconnectGraceSeconds")));
	if (!server.start(&error)) {
		QTextStream(stderr) << "Unable to start game server: " << error << '\n';
		return 1;
	}
	QTextStream(stdout) << "Room " << room.roomName << " (" << room.roomId << ") listening on port "
						<< server.port() << '\n';
	return application.exec();
}
