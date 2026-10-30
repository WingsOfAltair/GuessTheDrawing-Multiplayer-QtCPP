#pragma once

#include "shared/app_config.h"
#include "shared/models.h"

#include <QAudioFormat>
#include <QJsonArray>
#include <QMainWindow>
#include <QHash>
#include <QElapsedTimer>
#include <QPointer>
#include <QPoint>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

class QCheckBox;
class QDateTimeEdit;
class QDialog;
class QEvent;
class QAudioSink;
class QAudioSource;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTableWidget;
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
class UpdateManager;

class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(AppConfig& config, QString masterHost, quint16 masterPort, QWidget* parent = nullptr);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

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
	void showModerationNotice(const QJsonObject& message);
	void requestRooms();
	void searchUsers();
	void requestSocialList();
	void requestInboxList();
	void setPresenceStatus(const QString& status);
	void sortFriendsList();
	void deleteInboxConversation(const QString& targetUsername);
	void deleteInboxMessage(qint64 messageId);
	void sendSocialAction(const QString& action, const QString& targetUsername = {}, qint64 requestId = 0,
		bool blocked = false);
	void clearNotification(QListWidgetItem* item);
	void updateSocialTabTitle();
	void openInbox(const QString& targetUsername);
	void markInboxConversationRead(const QString& targetUsername = {});
	void showUserDirectoryEntry(const UserDirectoryEntry& entry);
	void joinRoom(const RoomInfo& room, bool spectator);
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
	void updateSpectatorsList();
	void showSpectatorsScreen();
	void showRosterContextMenu(const QPoint& position, QListWidget* sourceList = nullptr);
	void showModerationScreen();
	void requestServerAdminData();
	void refreshModerationPermissions();
	void showPlayerProfile(const QString& username);
	void sendPlayerMute(const QString& targetUsername, bool textChat, bool voiceChat);
	void sendModerationAction(const QString& targetUsername, const QString& action, qint64 sanctionId = 0);
	void sendStaffRole(const QString& targetUsername, const QString& role);
	void updateQueueStatus(const QJsonObject& queue);
	void updateRoomsRefreshStatus();
	void probeRooms(const QJsonArray& rooms);
	void finishRoomPingProbe(QTcpSocket* socket, int pingMs = -1);
	void sendActivePing();
	void scheduleGameReconnect();
	void reconnectGameServer();
	void stopGameReconnect();
	void requestLeaderboards();
	void installPendingUpdate();
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
	QTimer gameReconnectTimer_;
	QTimer canvasOperationFlushTimer_;
	QHash<QString, QJsonObject> pendingCanvasOperations_;
	QAudioSource* voiceInput_ = nullptr;
	QIODevice* voiceInputDevice_ = nullptr;
	QHash<QString, QAudioSink*> voiceOutputs_;
	QHash<QString, QIODevice*> voiceOutputDevices_;
	QHash<QString, QAudioFormat> voiceOutputFormats_;
	QHash<QString, QByteArray> voiceOutputPending_;
	QTimer voiceOutputFlushTimer_;
	QTimer updateCheckTimer_;
	QTimer presenceIdleTimer_;
	UpdateManager* updateManager_ = nullptr;
	QString pendingUpdateArchive_;
	QString pendingUpdateVersion_;
	bool updateRestartLaunched_ = false;
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
	QTabWidget* lobbyTabs_ = nullptr;
	QWidget* socialPage_ = nullptr;
	QWidget* loginPage_ = nullptr;
	QWidget* lobbyPage_ = nullptr;
	QWidget* gamePage_ = nullptr;
	QLineEdit* usernameEdit_ = nullptr;
	QLineEdit* passwordEdit_ = nullptr;
	QLineEdit* displayNameEdit_ = nullptr;
	QCheckBox* createAccountCheck_ = nullptr;
	QLabel* loginStatusLabel_ = nullptr;
	QLabel* loginConnectionStatusLabel_ = nullptr;
	QProgressBar* loginProgressBar_ = nullptr;
	QProgressBar* updateProgressBar_ = nullptr;
	QLabel* connectionStatusLabel_ = nullptr;
	QProgressBar* lobbyProgressBar_ = nullptr;
	QLineEdit* userSearchEdit_ = nullptr;
	QListWidget* userSearchResults_ = nullptr;
	QListWidget* friendsList_ = nullptr;
	QListWidget* notificationsList_ = nullptr;
	QListWidget* inboxContactsList_ = nullptr;
	QLabel* inboxUnreadLabel_ = nullptr;
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
	QProgressBar* profileProgressBar_ = nullptr;
	QLabel* settingsStatusLabel_ = nullptr;
	QPushButton* joinRoomButton_ = nullptr;
	QComboBox* joinRoleCombo_ = nullptr;
	QLabel* gameRoomLabel_ = nullptr;
	QPushButton* moderationButton_ = nullptr;
	QLabel* queueStatusLabel_ = nullptr;
	QProgressBar* queueProgressBar_ = nullptr;
	QLabel* roundStatusLabel_ = nullptr;
	QLabel* wordLabel_ = nullptr;
	QLabel* timerLabel_ = nullptr;
	DrawingCanvas* canvas_ = nullptr;
	QListWidget* rosterList_ = nullptr;
	QPushButton* showSpectatorsButton_ = nullptr;
	QPushButton* inviteFriendButton_ = nullptr;
	QPushButton* roomFriendRequestsButton_ = nullptr;
	QPushButton* roomInboxButton_ = nullptr;
	QPushButton* roomNotificationsButton_ = nullptr;
	QPointer<QDialog> spectatorsDialog_;
	QPointer<QListWidget> spectatorsList_;
	QPointer<QDialog> roomFriendRequestsDialog_;
	QPointer<QListWidget> roomFriendRequestsList_;
	QPointer<QDialog> roomInboxDialog_;
	QPointer<QListWidget> roomInboxList_;
	QPointer<QDialog> roomNotificationsDialog_;
	QPointer<QListWidget> roomNotificationsList_;
	QPointer<QDialog> inboxDialog_;
	QPointer<QListWidget> inboxMessages_;
	QPointer<QLineEdit> inboxInput_;
	QDialog* moderationDialog_ = nullptr;
	QListWidget* bannedPlayersList_ = nullptr;
	QListWidget* serverMutesList_ = nullptr;
	QListWidget* serverStaffList_ = nullptr;
	QLabel* moderationStatusLabel_ = nullptr;
	QLineEdit* moderationStaffUsernameEdit_ = nullptr;
	QComboBox* moderationStaffRoleCombo_ = nullptr;
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
	QString presenceStatus_ = QStringLiteral("active");
	QString languageCode_ = QStringLiteral("en");
	QString username_;
	QString currentServerRole_ = QStringLiteral("player");
	QSet<QString> mutedTextUsers_;
	QSet<QString> mutedVoiceUsers_;
	QString currentDrawer_;
	QJsonArray roomRoster_;
	bool chatBlockedThisRound_ = false;
	QString pendingAuthRequestId_;
	QString pendingRoomsRequestId_;
	QString pendingInboxRequestId_;
	QString inboxTargetUsername_;
	bool inboxAtBottom_ = false;
	QString pendingUserProfileRequestId_;
	QHash<QString, int> inboxUnreadCounts_;
	QSet<qint64> knownNotificationIds_;
	bool notificationListInitialized_ = false;
	int notificationCount_ = 0;
	int friendRequestCount_ = 0;
	QString pendingJoinRequestId_;
	QString pendingRoleChangeRequestId_;
	QString pendingActivePingRequestId_;
	QString pendingRoomId_;
	QString pendingRoomPasskey_;
	RoomInfo joiningRoom_;
	QHash<QString, RoomInfo> roomsById_;
	quint64 requestCounter_ = 1;
	int secondsRemaining_ = 0;
	int activePingMs_ = -1;
	bool roundActive_ = false;
	bool roundStarting_ = false;
	bool inRoom_ = false;
	bool isSpectator_ = false;
	bool queuedForPlayer_ = false;
	bool pendingJoinAsSpectator_ = false;
	bool leavingRoom_ = false;
	bool reconnectingGameServer_ = false;
	bool refreshingLayers_ = false;
	bool masterSuperAdmin_ = false;
};

}
