#pragma once

#include "shared/app_config.h"
#include "shared/models.h"

#include <QAudioFormat>
#include <QJsonArray>
#include <QMainWindow>
#include <QHash>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

class QCheckBox;
class QAudioSink;
class QAudioSource;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QIODevice;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QWidget;

namespace gtd {

class DrawingCanvas;
class LatencyIndicator;

class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(AppConfig& config, QString masterHost, quint16 masterPort, QWidget* parent = nullptr);

private:
	QWidget* createLoginPage();
	QWidget* createLobbyPage();
	QWidget* createGamePage();
	void connectMaster();
	void readMaster();
	void readGameServer();
	void submitAuthentication();
	void handleMasterMessage(const QJsonObject& message);
	void handleGameMessage(const QJsonObject& message);
	void requestRooms();
	void populateRooms(const QJsonArray& rooms);
	void joinSelectedRoom();
	void leaveRoom();
	void sendGameMessage(const QJsonObject& message);
	void flushPendingCanvasOperations();
	void sendGuess();
	void sendRoomChat();
	void startVoiceInput();
	void stopVoiceInput();
	void clearVoiceOutputs();
	void flushVoiceOutputs();
	void handleVoiceFrame(const QJsonObject& message);
	void appendGameMessage(const QString& message);
	void updateRoundControls();
	void updateRoster(const QJsonArray& players, const QJsonObject& room);
	void updateQueueStatus(const QJsonObject& queue);
	void updateRoomsRefreshStatus();
	void probeRooms(const QJsonArray& rooms);
	void finishRoomPingProbe(QTcpSocket* socket, int pingMs = -1);
	void sendActivePing();
	void requestLeaderboards();
	void populateLeaderboard(QTableWidget* table, const QJsonArray& players, bool drawingPoints);
	void saveProfile();
	void applyLanguage(const QString& languageCode);
	void applyTheme(const QString& theme);
	void updateLayerList(const QJsonArray& layers);
	void chooseTextAt(const QPointF& position);
	void chooseColor();
	void updateColorButton(const QString& color);
	QString nextRequestId();

	AppConfig& config_;
	QString masterHost_;
	quint16 masterPort_ = 0;
	QTcpSocket masterSocket_;
	QTcpSocket gameSocket_;
	QByteArray masterBuffer_;
	QByteArray gameBuffer_;
	QTimer roomRefreshTimer_;
	QTimer refreshAgeTimer_;
	QTimer activePingTimer_;
	QTimer canvasOperationFlushTimer_;
	QHash<QString, QJsonObject> pendingCanvasOperations_;
	QAudioSource* voiceInput_ = nullptr;
	QIODevice* voiceInputDevice_ = nullptr;
	QHash<QString, QAudioSink*> voiceOutputs_;
	QHash<QString, QIODevice*> voiceOutputDevices_;
	QHash<QString, QAudioFormat> voiceOutputFormats_;
	QHash<QString, QByteArray> voiceOutputPending_;
	QTimer voiceOutputFlushTimer_;
	QByteArray voiceCaptureBuffer_;
	QByteArray voiceInputBuffer_;
	QAudioFormat voiceInputFormat_;
	QVector<float> voiceResampleInput_;
	double voiceResamplePosition_ = 0.0;
	QElapsedTimer roomsRefreshElapsed_;
	QElapsedTimer activePingElapsed_;
	struct RoomPingProbe {
		QString roomId;
		QString requestId;
		QByteArray buffer;
		QElapsedTimer elapsed;
	};
	QHash<QTcpSocket*, RoomPingProbe> roomPingProbes_;
	QHash<QString, QTcpSocket*> roomPingSockets_;
	QHash<QString, int> roomPingMs_;
	QHash<QString, LatencyIndicator*> roomLatencyIndicators_;
	QStackedWidget* pages_ = nullptr;
	QWidget* loginPage_ = nullptr;
	QWidget* lobbyPage_ = nullptr;
	QWidget* gamePage_ = nullptr;
	QLineEdit* usernameEdit_ = nullptr;
	QLineEdit* passwordEdit_ = nullptr;
	QLineEdit* displayNameEdit_ = nullptr;
	QCheckBox* createAccountCheck_ = nullptr;
	QLabel* loginStatusLabel_ = nullptr;
	QLabel* loginConnectionStatusLabel_ = nullptr;
	QLabel* connectionStatusLabel_ = nullptr;
	QTableWidget* roomsTable_ = nullptr;
	LatencyIndicator* activeLatencyIndicator_ = nullptr;
	QTableWidget* guessingLeaderboardTable_ = nullptr;
	QTableWidget* drawingLeaderboardTable_ = nullptr;
	QComboBox* countryCombo_ = nullptr;
	QComboBox* languageCombo_ = nullptr;
	QComboBox* themeCombo_ = nullptr;
	QLineEdit* nicknameEdit_ = nullptr;
	QLineEdit* currentPasswordEdit_ = nullptr;
	QLineEdit* newPasswordEdit_ = nullptr;
	QLabel* profileStatusLabel_ = nullptr;
	QLabel* settingsStatusLabel_ = nullptr;
	QPushButton* joinRoomButton_ = nullptr;
	QComboBox* joinRoleCombo_ = nullptr;
	QLabel* gameRoomLabel_ = nullptr;
	QLabel* queueStatusLabel_ = nullptr;
	QProgressBar* queueProgressBar_ = nullptr;
	QLabel* roundStatusLabel_ = nullptr;
	QLabel* wordLabel_ = nullptr;
	QLabel* timerLabel_ = nullptr;
	DrawingCanvas* canvas_ = nullptr;
	QListWidget* rosterList_ = nullptr;
	QListWidget* chatList_ = nullptr;
	QLineEdit* guessEdit_ = nullptr;
	QPushButton* sendGuessButton_ = nullptr;
	QLineEdit* roomChatEdit_ = nullptr;
	QPushButton* sendChatButton_ = nullptr;
	QPushButton* voiceButton_ = nullptr;
	QLabel* voiceStatusLabel_ = nullptr;
	QPushButton* clearLayerButton_ = nullptr;
	QPushButton* clearCanvasButton_ = nullptr;
	QPushButton* undoButton_ = nullptr;
	QPushButton* redoButton_ = nullptr;
	QPushButton* promoteToPlayerButton_ = nullptr;
	QComboBox* toolCombo_ = nullptr;
	QPushButton* colorButton_ = nullptr;
	QSpinBox* strokeWidthSpin_ = nullptr;
	QSpinBox* fontSizeSpin_ = nullptr;
	QCheckBox* filledCheck_ = nullptr;
	QListWidget* layerList_ = nullptr;
	QPushButton* addLayerButton_ = nullptr;
	QPushButton* deleteLayerButton_ = nullptr;
	QPushButton* renameLayerButton_ = nullptr;
	QPushButton* visibilityLayerButton_ = nullptr;
	QPushButton* lockLayerButton_ = nullptr;
	QPushButton* layerUpButton_ = nullptr;
	QPushButton* layerDownButton_ = nullptr;
	QString authToken_;
	QString languageCode_ = QStringLiteral("en");
	QString username_;
	QString currentDrawer_;
	QJsonArray roomRoster_;
	QString pendingAuthRequestId_;
	QString pendingJoinRequestId_;
	QString pendingRoleChangeRequestId_;
	QString pendingActivePingRequestId_;
	QString pendingRoomId_;
	RoomInfo joiningRoom_;
	QHash<QString, RoomInfo> roomsById_;
	quint64 requestCounter_ = 1;
	int secondsRemaining_ = 0;
	int activePingMs_ = -1;
	bool roundActive_ = false;
	bool inRoom_ = false;
	bool isSpectator_ = false;
	bool queuedForPlayer_ = false;
	bool pendingJoinAsSpectator_ = false;
	bool leavingRoom_ = false;
	bool refreshingLayers_ = false;
};

}
