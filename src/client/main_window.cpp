#include "client/main_window.h"

#include "client/drawing_canvas.h"
#include "shared/protocol.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QPainter>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector>

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gtd {

namespace {

int audioBytesPerSample(QAudioFormat::SampleFormat sampleFormat)
{
	switch (sampleFormat) {
	case QAudioFormat::UInt8: return 1;
	case QAudioFormat::Int16: return 2;
	case QAudioFormat::Int32:
	case QAudioFormat::Float: return 4;
	default: return 0;
	}
}

bool usableAudioFormat(const QAudioFormat& format)
{
	return format.isValid() && format.sampleRate() > 0 && format.channelCount() > 0
		&& audioBytesPerSample(format.sampleFormat()) > 0;
}

float decodeAudioSample(const char* data, const QAudioFormat& format)
{
	switch (format.sampleFormat()) {
	case QAudioFormat::UInt8:
		return (static_cast<unsigned char>(*data) - 128.0f) / 128.0f;
	case QAudioFormat::Int16: {
		qint16 sample;
		std::memcpy(&sample, data, sizeof(sample));
		return static_cast<float>(sample) / 32768.0f;
	}
	case QAudioFormat::Int32: {
		qint32 sample;
		std::memcpy(&sample, data, sizeof(sample));
		return static_cast<float>(sample) / 2147483648.0f;
	}
	case QAudioFormat::Float: {
		float sample;
		std::memcpy(&sample, data, sizeof(sample));
		return std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
	}
	default:
		return 0.0f;
	}
}

void encodeAudioSample(QByteArray& destination, float sample, const QAudioFormat& format)
{
	sample = std::clamp(sample, -1.0f, 1.0f);
	switch (format.sampleFormat()) {
	case QAudioFormat::UInt8:
		destination.append(static_cast<char>(std::clamp(qRound(sample * 127.0f + 128.0f), 0, 255)));
		break;
	case QAudioFormat::Int16: {
		const qint16 value = static_cast<qint16>(std::clamp(qRound(sample * 32767.0f), -32768, 32767));
		char bytes[sizeof(value)];
		std::memcpy(bytes, &value, sizeof(value));
		destination.append(bytes, sizeof(bytes));
		break;
	}
	case QAudioFormat::Int32: {
		const qint32 value = static_cast<qint32>(std::clamp(static_cast<double>(sample) * 2147483647.0,
			-2147483648.0, 2147483647.0));
		char bytes[sizeof(value)];
		std::memcpy(bytes, &value, sizeof(value));
		destination.append(bytes, sizeof(bytes));
		break;
	}
	case QAudioFormat::Float: {
		char bytes[sizeof(float)];
		std::memcpy(bytes, &sample, sizeof(sample));
		destination.append(bytes, sizeof(bytes));
		break;
	}
	default:
		break;
	}
}

QByteArray convertServerFrameForOutput(const QByteArray& pcm, const QAudioFormat& format)
{
	constexpr int sourceSamples = 320;
	const int outputFrames = qMax(1, qRound(sourceSamples * format.sampleRate() / 16000.0));
	QByteArray converted;
	converted.reserve(outputFrames * format.channelCount() * audioBytesPerSample(format.sampleFormat()));
	for (int frame = 0; frame < outputFrames; ++frame) {
		const double sourcePosition = frame * 16000.0 / format.sampleRate();
		const int first = qBound(0, static_cast<int>(sourcePosition), sourceSamples - 1);
		const int second = qMin(first + 1, sourceSamples - 1);
		const float fraction = static_cast<float>(sourcePosition - first);
		const float firstSample = static_cast<float>(qFromLittleEndian<qint16>(
			reinterpret_cast<const uchar*>(pcm.constData() + first * 2))) / 32768.0f;
		const float secondSample = static_cast<float>(qFromLittleEndian<qint16>(
			reinterpret_cast<const uchar*>(pcm.constData() + second * 2))) / 32768.0f;
		const float sample = firstSample + (secondSample - firstSample) * fraction;
		for (int channel = 0; channel < format.channelCount(); ++channel)
			encodeAudioSample(converted, sample, format);
	}
	return converted;
}

} // namespace

class LatencyIndicator final : public QWidget {
public:
	explicit LatencyIndicator(QWidget* parent = nullptr)
		: QWidget(parent)
	{
		setFixedSize(105, 24);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setPing(-1);
	}

	void setPing(int pingMs)
	{
		pingMs_ = pingMs;
		const QString description = pingMs_ < 0
			? QStringLiteral("Ping unavailable")
			: QStringLiteral("%1 ms · %2 latency").arg(pingMs_).arg(quality());
		setToolTip(description);
		setAccessibleName(description);
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const int bars = strength();
		QColor activeColor(QStringLiteral("#43a047"));
		if (pingMs_ > 200)
			activeColor = QColor(QStringLiteral("#e53935"));
		else if (pingMs_ > 120)
			activeColor = QColor(QStringLiteral("#f9a825"));
		else if (pingMs_ > 60)
			activeColor = QColor(QStringLiteral("#7cb342"));
		const QColor inactiveColor = palette().color(QPalette::Disabled, QPalette::Text);
		const int heights[] = {5, 8, 11, 14};
		for (int index = 0; index < 4; ++index) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(index < bars ? activeColor : inactiveColor);
			painter.drawRoundedRect(4 + index * 6, height() - heights[index] - 3,
				4, heights[index], 1, 1);
		}
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(31, height() - 5, pingMs_ < 0
			? QStringLiteral("— ms") : QStringLiteral("%1 ms").arg(pingMs_));
	}

private:
	int strength() const
	{
		if (pingMs_ < 0)
			return 0;
		if (pingMs_ <= 60)
			return 4;
		if (pingMs_ <= 120)
			return 3;
		if (pingMs_ <= 200)
			return 2;
		return 1;
	}

	QString quality() const
	{
		if (pingMs_ < 0)
			return QStringLiteral("unavailable");
		if (pingMs_ <= 60)
			return QStringLiteral("excellent");
		if (pingMs_ <= 120)
			return QStringLiteral("good");
		if (pingMs_ <= 200)
			return QStringLiteral("fair");
		return QStringLiteral("weak");
	}

	int pingMs_ = -1;
};

MainWindow::MainWindow(QString masterHost, quint16 masterPort, QWidget* parent)
	: QMainWindow(parent),
	  masterHost_(std::move(masterHost)),
	  masterPort_(masterPort)
{
	setWindowTitle(QStringLiteral("Guess The Drawing"));
	resize(1100, 720);

	pages_ = new QStackedWidget(this);
	loginPage_ = createLoginPage();
	lobbyPage_ = createLobbyPage();
	gamePage_ = createGamePage();
	pages_->addWidget(loginPage_);
	pages_->addWidget(lobbyPage_);
	pages_->addWidget(gamePage_);
	setCentralWidget(pages_);
	pages_->setCurrentWidget(loginPage_);

	connect(&masterSocket_, &QTcpSocket::connected, this, [this] {
		loginConnectionStatusLabel_->setText(QStringLiteral("Connected to master"));
		connectionStatusLabel_->setText(QStringLiteral("Connected to master"));
		if (!authToken_.isEmpty()) {
			requestRooms();
			requestLeaderboards();
		}
	});
	connect(&masterSocket_, &QTcpSocket::readyRead, this, [this] { readMaster(); });
	connect(&masterSocket_, &QTcpSocket::disconnected, this, [this] {
		connectionStatusLabel_->setText(QStringLiteral("Master disconnected; reconnecting…"));
		loginConnectionStatusLabel_->setText(QStringLiteral("Master disconnected; reconnecting…"));
		QTimer::singleShot(2000, this, [this] { connectMaster(); });
	});
	connect(&masterSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		const QString message = QStringLiteral("Cannot reach master at %1:%2")
									.arg(masterHost_).arg(masterPort_);
		connectionStatusLabel_->setText(message);
		loginConnectionStatusLabel_->setText(message);
	});
	connect(&gameSocket_, &QTcpSocket::connected, this, [this] {
		pendingJoinRequestId_ = nextRequestId();
		sendGameMessage(makeMessage(QStringLiteral("JoinRoom"),
			{{QStringLiteral("requestId"), pendingJoinRequestId_},
			 {QStringLiteral("roomId"), pendingRoomId_}, {QStringLiteral("authToken"), authToken_},
			 {QStringLiteral("spectator"), pendingJoinAsSpectator_}}));
	});
	connect(&gameSocket_, &QTcpSocket::readyRead, this, [this] { readGameServer(); });
	connect(&gameSocket_, &QTcpSocket::disconnected, this, [this] {
		activePingTimer_.stop();
		stopVoiceInput();
		clearVoiceOutputs();
		pendingActivePingRequestId_.clear();
		activePingMs_ = -1;
		if (activeLatencyIndicator_)
			activeLatencyIndicator_->setPing(-1);
		if (inRoom_ && !leavingRoom_) {
			inRoom_ = false;
			pages_->setCurrentWidget(lobbyPage_);
			connectionStatusLabel_->setText(QStringLiteral("Disconnected from the game server"));
			requestRooms();
		}
		leavingRoom_ = false;
	});
	connect(&gameSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		if (!inRoom_)
			connectionStatusLabel_->setText(QStringLiteral("Could not connect to the selected room"));
	});
	activePingTimer_.setInterval(2000);
	connect(&activePingTimer_, &QTimer::timeout, this, [this] { sendActivePing(); });
	voiceOutputFlushTimer_.setInterval(5);
	connect(&voiceOutputFlushTimer_, &QTimer::timeout, this, [this] { flushVoiceOutputs(); });

	roomRefreshTimer_.setInterval(5000);
	connect(&roomRefreshTimer_, &QTimer::timeout, this, [this] {
		if (pages_->currentWidget() == lobbyPage_)
			requestRooms();
	});
	refreshAgeTimer_.setInterval(1000);
	connect(&refreshAgeTimer_, &QTimer::timeout, this, [this] { updateRoomsRefreshStatus(); });
	refreshAgeTimer_.start();
	connectMaster();
}

QWidget* MainWindow::createLoginPage()
{
	auto* page = new QWidget(this);
	auto* outer = new QVBoxLayout(page);
	outer->addStretch();
	auto* title = new QLabel(QStringLiteral("Guess The Drawing"), page);
	QFont titleFont = title->font();
	titleFont.setPointSize(26);
	titleFont.setBold(true);
	title->setFont(titleFont);
	title->setAlignment(Qt::AlignCenter);
	outer->addWidget(title);
	auto* form = new QFormLayout;
	usernameEdit_ = new QLineEdit(page);
	usernameEdit_->setMaxLength(24);
	usernameEdit_->setPlaceholderText(QStringLiteral("3-24 letters, numbers, or underscores"));
	passwordEdit_ = new QLineEdit(page);
	passwordEdit_->setEchoMode(QLineEdit::Password);
	passwordEdit_->setMaxLength(128);
	displayNameEdit_ = new QLineEdit(page);
	displayNameEdit_->setMaxLength(32);
	displayNameEdit_->setPlaceholderText(QStringLiteral("Shown to other players"));
	displayNameEdit_->hide();
	form->addRow(QStringLiteral("Username"), usernameEdit_);
	form->addRow(QStringLiteral("Password"), passwordEdit_);
	form->addRow(QStringLiteral("Display name"), displayNameEdit_);
	outer->addLayout(form);
	createAccountCheck_ = new QCheckBox(QStringLiteral("Create a new account"), page);
	outer->addWidget(createAccountCheck_);
	connect(createAccountCheck_, &QCheckBox::toggled, this, [this](bool checked) {
		displayNameEdit_->setVisible(checked);
	});
	auto* continueButton = new QPushButton(QStringLiteral("Continue"), page);
	continueButton->setDefault(true);
	outer->addWidget(continueButton);
	loginStatusLabel_ = new QLabel(page);
	loginStatusLabel_->setWordWrap(true);
	outer->addWidget(loginStatusLabel_);
	loginConnectionStatusLabel_ = new QLabel(QStringLiteral("Connecting to master…"), page);
	loginConnectionStatusLabel_->setAlignment(Qt::AlignCenter);
	outer->addWidget(loginConnectionStatusLabel_);
	outer->addStretch();
	connect(continueButton, &QPushButton::clicked, this, [this] { submitAuthentication(); });
	connect(passwordEdit_, &QLineEdit::returnPressed, this, [this] { submitAuthentication(); });
	return page;
}

QWidget* MainWindow::createLobbyPage()
{
	auto* page = new QWidget(this);
	auto* layout = new QVBoxLayout(page);
	auto* heading = new QLabel(QStringLiteral("Lobby"), page);
	QFont font = heading->font();
	font.setPointSize(20);
	font.setBold(true);
	heading->setFont(font);
	layout->addWidget(heading);
	auto* topRow = new QGridLayout;
	auto* accountLabel = new QLabel(page);
	accountLabel->setText(QStringLiteral("Signed in as %1").arg(username_));
	accountLabel->setObjectName(QStringLiteral("accountLabel"));
	topRow->addWidget(accountLabel, 0, 0);
	connectionStatusLabel_ = new QLabel(QStringLiteral("Connecting to master…"), page);
	connectionStatusLabel_->setObjectName(QStringLiteral("lobbyStatusLabel"));
	topRow->addWidget(connectionStatusLabel_, 0, 1);
	countryCombo_ = new QComboBox(page);
	countryCombo_->setObjectName(QStringLiteral("countryCombo"));
	countryCombo_->addItem(QStringLiteral("Choose country…"), QString());
	const QStringList countryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FM FO FR GA GB GD GE GF GG GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT "
		"HU ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS "
		"LT LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI "
		"NL NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG "
		"SH SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG "
		"UM US UY UZ VA VC VE VG VI VN VU WF WS YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (const QString& countryCode : countryCodes)
		countryCombo_->addItem(countryCode, countryCode);
	auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), page);
	topRow->addWidget(refreshButton, 0, 2);
	layout->addLayout(topRow);
	auto* tabs = new QTabWidget(page);
	auto* roomsPage = new QWidget(tabs);
	auto* roomsLayout = new QVBoxLayout(roomsPage);
	roomsTable_ = new QTableWidget(0, 5, page);
	roomsTable_->setHorizontalHeaderLabels({QStringLiteral("Room"), QStringLiteral("Players"),
											 QStringLiteral("Round timer"), QStringLiteral("Ping"),
											 QStringLiteral("Server")});
	roomsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	roomsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	roomsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	// A cell widget alone does not reliably contribute to ResizeToContents sizing.
	// Keep the Ping column wide enough for the latency bars and millisecond value.
	roomsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
	roomsTable_->setColumnWidth(3, 120);
	roomsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	roomsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	roomsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
	roomsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	roomsLayout->addWidget(roomsTable_, 1);
	auto* bottomRow = new QGridLayout;
	auto* hostInstructions = new QLabel(
		QStringLiteral("Rooms are hosted by dedicated game-server processes."), page);
	bottomRow->addWidget(hostInstructions, 0, 0);
	joinRoomButton_ = new QPushButton(QStringLiteral("Join selected room"), page);
	joinRoomButton_->setEnabled(false);
	joinRoleCombo_ = new QComboBox(page);
	joinRoleCombo_->addItem(QStringLiteral("Join as player"), false);
	joinRoleCombo_->addItem(QStringLiteral("Join as spectator"), true);
	bottomRow->addWidget(joinRoleCombo_, 0, 1);
	bottomRow->addWidget(joinRoomButton_, 0, 2);
	auto* signOutButton = new QPushButton(QStringLiteral("Sign out"), page);
	bottomRow->addWidget(signOutButton, 0, 3);
	roomsLayout->addLayout(bottomRow);
	tabs->addTab(roomsPage, QStringLiteral("Rooms"));
	auto* leaderboardPage = new QWidget(tabs);
	auto* leaderboardLayout = new QVBoxLayout(leaderboardPage);
	auto* leaderboardTabs = new QTabWidget(leaderboardPage);
	guessingLeaderboardTable_ = new QTableWidget(0, 4, leaderboardTabs);
	guessingLeaderboardTable_->setHorizontalHeaderLabels({QStringLiteral("Rank"), QStringLiteral("Player"),
		QStringLiteral("Country"), QStringLiteral("Guessing points")});
	drawingLeaderboardTable_ = new QTableWidget(0, 4, leaderboardTabs);
	drawingLeaderboardTable_->setHorizontalHeaderLabels({QStringLiteral("Rank"), QStringLiteral("Player"),
		QStringLiteral("Country"), QStringLiteral("Drawing points")});
	for (QTableWidget* table : {guessingLeaderboardTable_, drawingLeaderboardTable_}) {
		table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	}
	leaderboardTabs->addTab(guessingLeaderboardTable_, QStringLiteral("Guessing"));
	leaderboardTabs->addTab(drawingLeaderboardTable_, QStringLiteral("Drawing"));
	leaderboardLayout->addWidget(leaderboardTabs);
	tabs->addTab(leaderboardPage, QStringLiteral("Leaderboards"));
	auto* profilePage = new QWidget(tabs);
	auto* profileLayout = new QFormLayout(profilePage);
	nicknameEdit_ = new QLineEdit(profilePage);
	nicknameEdit_->setMaxLength(32);
	profileLayout->addRow(QStringLiteral("Nickname"), nicknameEdit_);
	profileLayout->addRow(QStringLiteral("Country / flag"), countryCombo_);
	currentPasswordEdit_ = new QLineEdit(profilePage);
	currentPasswordEdit_->setEchoMode(QLineEdit::Password);
	currentPasswordEdit_->setMaxLength(128);
	currentPasswordEdit_->setPlaceholderText(QStringLiteral("Required to change password"));
	profileLayout->addRow(QStringLiteral("Current password"), currentPasswordEdit_);
	newPasswordEdit_ = new QLineEdit(profilePage);
	newPasswordEdit_->setEchoMode(QLineEdit::Password);
	newPasswordEdit_->setMaxLength(128);
	newPasswordEdit_->setPlaceholderText(QStringLiteral("Leave blank to keep current password"));
	profileLayout->addRow(QStringLiteral("New password"), newPasswordEdit_);
	auto* saveProfileButton = new QPushButton(QStringLiteral("Save profile"), profilePage);
	profileStatusLabel_ = new QLabel(profilePage);
	profileStatusLabel_->setWordWrap(true);
	auto* profileActions = new QHBoxLayout;
	profileActions->addWidget(saveProfileButton);
	profileActions->addWidget(profileStatusLabel_, 1);
	profileLayout->addRow(profileActions);
	tabs->addTab(profilePage, QStringLiteral("Profile"));
	layout->addWidget(tabs, 1);
	connect(refreshButton, &QPushButton::clicked, this, [this] { requestRooms(); });
	connect(saveProfileButton, &QPushButton::clicked, this, [this] { saveProfile(); });
	connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
		if (index == 1)
			requestLeaderboards();
	});
	connect(joinRoomButton_, &QPushButton::clicked, this, [this] { joinSelectedRoom(); });
	connect(roomsTable_, &QTableWidget::itemSelectionChanged, this, [this] {
		joinRoomButton_->setEnabled(roomsTable_->currentRow() >= 0);
	});
	connect(roomsTable_, &QTableWidget::itemDoubleClicked, this, [this] { joinSelectedRoom(); });
	connect(signOutButton, &QPushButton::clicked, this, [this] {
		if (masterSocket_.state() == QAbstractSocket::ConnectedState)
			sendMessage(&masterSocket_, makeMessage(QStringLiteral("Logout")));
		authToken_.clear();
		pages_->setCurrentWidget(loginPage_);
		roomRefreshTimer_.stop();
	});
	return page;
}

QWidget* MainWindow::createGamePage()
{
	auto* page = new QWidget(this);
	auto* layout = new QVBoxLayout(page);
	auto* header = new QGridLayout;
	gameRoomLabel_ = new QLabel(page);
	auto* activePingWidget = new QWidget(page);
	auto* activePingLayout = new QHBoxLayout(activePingWidget);
	activePingLayout->setContentsMargins(0, 0, 0, 0);
	activePingLayout->setSpacing(4);
	activePingLayout->addWidget(new QLabel(QStringLiteral("Ping"), activePingWidget));
	activeLatencyIndicator_ = new LatencyIndicator(activePingWidget);
	activePingLayout->addWidget(activeLatencyIndicator_);
	queueStatusLabel_ = new QLabel(page);
	queueStatusLabel_->hide();
	queueProgressBar_ = new QProgressBar(page);
	queueProgressBar_->setAccessibleName(QStringLiteral("Queue progress"));
	queueProgressBar_->setToolTip(QStringLiteral("Progress toward the front of the player queue"));
	queueProgressBar_->hide();
	roundStatusLabel_ = new QLabel(QStringLiteral("Waiting for players"), page);
	wordLabel_ = new QLabel(QStringLiteral("Your word: —"), page);
	timerLabel_ = new QLabel(QStringLiteral("—:—"), page);
	timerLabel_->setAlignment(Qt::AlignCenter);
	QFont timerFont = timerLabel_->font();
	timerFont.setPointSize(18);
	timerFont.setBold(true);
	timerLabel_->setFont(timerFont);
	auto* leaveButton = new QPushButton(QStringLiteral("Leave room"), page);
	promoteToPlayerButton_ = new QPushButton(QStringLiteral("Join as Player"), page);
	promoteToPlayerButton_->hide();
	header->addWidget(gameRoomLabel_, 0, 0);
	header->addWidget(roundStatusLabel_, 0, 1);
	header->addWidget(wordLabel_, 0, 2);
	header->addWidget(timerLabel_, 0, 3);
	header->addWidget(activePingWidget, 0, 4);
	header->addWidget(promoteToPlayerButton_, 0, 5);
	header->addWidget(leaveButton, 0, 6);
	layout->addLayout(header);
	layout->addWidget(queueStatusLabel_);
	layout->addWidget(queueProgressBar_);

	auto* toolbar = new QHBoxLayout;
	toolCombo_ = new QComboBox(page);
	const QList<QPair<QString, DrawingCanvas::Tool>> tools{
		{QStringLiteral("Select / move / scale"), DrawingCanvas::Tool::Select},
		{QStringLiteral("Pen"), DrawingCanvas::Tool::Pen},
		{QStringLiteral("Eraser · cut strokes"), DrawingCanvas::Tool::Eraser},
		{QStringLiteral("Object Eraser"), DrawingCanvas::Tool::ObjectEraser},
		{QStringLiteral("Rectangle"), DrawingCanvas::Tool::Rectangle},
		{QStringLiteral("Ellipse"), DrawingCanvas::Tool::Ellipse},
		{QStringLiteral("Line"), DrawingCanvas::Tool::Line},
		{QStringLiteral("Text"), DrawingCanvas::Tool::Text}};
	for (const auto& tool : tools)
		toolCombo_->addItem(tool.first, static_cast<int>(tool.second));
	toolbar->addWidget(toolCombo_);
	colorButton_ = new QPushButton(QStringLiteral("Color"), page);
	colorButton_->setStyleSheet(QStringLiteral("background-color: #202124; color: white"));
	toolbar->addWidget(colorButton_);
	auto* widthLabel = new QLabel(QStringLiteral("Brush / eraser size"), page);
	widthLabel->setToolTip(QStringLiteral("The same size controls pen width and the stroke eraser radius."));
	toolbar->addWidget(widthLabel);
	strokeWidthSpin_ = new QSpinBox(page);
	strokeWidthSpin_->setRange(1, 32);
	strokeWidthSpin_->setValue(4);
	toolbar->addWidget(strokeWidthSpin_);
	toolbar->addWidget(new QLabel(QStringLiteral("Text size"), page));
	fontSizeSpin_ = new QSpinBox(page);
	fontSizeSpin_->setRange(8, 96);
	fontSizeSpin_->setValue(24);
	toolbar->addWidget(fontSizeSpin_);
	filledCheck_ = new QCheckBox(QStringLiteral("Fill shapes"), page);
	toolbar->addWidget(filledCheck_);
	toolCombo_->setCurrentIndex(1);
	clearLayerButton_ = new QPushButton(QStringLiteral("Clear Selected Layer"), page);
	toolbar->addWidget(clearLayerButton_);
	clearCanvasButton_ = new QPushButton(QStringLiteral("Clear All Layers"), page);
	toolbar->addWidget(clearCanvasButton_);
	toolbar->addStretch();
	layout->addLayout(toolbar);

	auto* body = new QHBoxLayout;
	canvas_ = new DrawingCanvas(page);
	canvas_->setTool(DrawingCanvas::Tool::Pen);
	body->addWidget(canvas_, 1);
	auto* sidePanel = new QVBoxLayout;
	sidePanel->addWidget(new QLabel(QStringLiteral("Layers · bottom to top"), page));
	layerList_ = new QListWidget(page);
	layerList_->setMinimumWidth(260);
	layerList_->setMaximumHeight(180);
	sidePanel->addWidget(layerList_);
	auto* layerButtons = new QGridLayout;
	addLayerButton_ = new QPushButton(QStringLiteral("Add"), page);
	deleteLayerButton_ = new QPushButton(QStringLiteral("Delete"), page);
	renameLayerButton_ = new QPushButton(QStringLiteral("Rename"), page);
	visibilityLayerButton_ = new QPushButton(QStringLiteral("Hide"), page);
	lockLayerButton_ = new QPushButton(QStringLiteral("Lock"), page);
	layerUpButton_ = new QPushButton(QStringLiteral("Move up"), page);
	layerDownButton_ = new QPushButton(QStringLiteral("Move down"), page);
	layerButtons->addWidget(addLayerButton_, 0, 0);
	layerButtons->addWidget(deleteLayerButton_, 0, 1);
	layerButtons->addWidget(renameLayerButton_, 1, 0);
	layerButtons->addWidget(visibilityLayerButton_, 1, 1);
	layerButtons->addWidget(lockLayerButton_, 2, 0);
	layerButtons->addWidget(layerUpButton_, 2, 1);
	layerButtons->addWidget(layerDownButton_, 3, 0, 1, 2);
	sidePanel->addLayout(layerButtons);
	sidePanel->addWidget(new QLabel(QStringLiteral("Players and spectators"), page));
	rosterList_ = new QListWidget(page);
	rosterList_->setMinimumHeight(90);
	rosterList_->setMaximumHeight(140);
	sidePanel->addWidget(rosterList_);
	sidePanel->addWidget(new QLabel(QStringLiteral("Room chat and guesses"), page));
	chatList_ = new QListWidget(page);
	chatList_->setMinimumHeight(100);
	sidePanel->addWidget(chatList_, 1);
	auto* guessRow = new QGridLayout;
	guessEdit_ = new QLineEdit(page);
	guessEdit_->setPlaceholderText(QStringLiteral("Type your guess"));
	guessEdit_->setMaxLength(100);
	sendGuessButton_ = new QPushButton(QStringLiteral("Guess"), page);
	guessRow->addWidget(guessEdit_, 0, 0);
	guessRow->addWidget(sendGuessButton_, 0, 1);
	sidePanel->addLayout(guessRow);
	auto* roomChatRow = new QHBoxLayout;
	roomChatEdit_ = new QLineEdit(page);
	roomChatEdit_->setPlaceholderText(QStringLiteral("Send a room message"));
	roomChatEdit_->setMaxLength(300);
	sendChatButton_ = new QPushButton(QStringLiteral("Send"), page);
	roomChatRow->addWidget(roomChatEdit_, 1);
	roomChatRow->addWidget(sendChatButton_);
	sidePanel->addLayout(roomChatRow);
	auto* voiceRow = new QHBoxLayout;
	voiceButton_ = new QPushButton(QStringLiteral("Enable microphone"), page);
	voiceButton_->setCheckable(true);
	voiceStatusLabel_ = new QLabel(QStringLiteral("Microphone off"), page);
	voiceRow->addWidget(voiceButton_);
	voiceRow->addWidget(voiceStatusLabel_, 1);
	sidePanel->addLayout(voiceRow);
	body->addLayout(sidePanel);
	layout->addLayout(body, 1);

	connect(canvas_, &DrawingCanvas::operationGenerated, this, [this](const QJsonObject& operation) {
		sendGameMessage(makeMessage(QStringLiteral("CanvasOperation"),
			{{QStringLiteral("operation"), operation}}));
	});
	connect(canvas_, &DrawingCanvas::layersChanged, this, [this](const QJsonArray& layers) {
		updateLayerList(layers);
	});
	connect(canvas_, &DrawingCanvas::selectedLayerChanged, this, [this](const QString& layerId) {
		for (int row = 0; row < layerList_->count(); ++row) {
			if (layerList_->item(row)->data(Qt::UserRole).toString() == layerId) {
				layerList_->setCurrentRow(row);
				break;
			}
		}
	});
	connect(canvas_, &DrawingCanvas::textRequested, this, [this](const QPointF& position) {
		chooseTextAt(position);
	});
	connect(toolCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		canvas_->setTool(static_cast<DrawingCanvas::Tool>(toolCombo_->itemData(index).toInt()));
	});
	connect(colorButton_, &QPushButton::clicked, this, [this] { chooseColor(); });
	connect(strokeWidthSpin_, &QSpinBox::valueChanged, this, [this](int width) {
		canvas_->setStrokeWidth(width);
	});
	connect(fontSizeSpin_, &QSpinBox::valueChanged, this, [this](int size) {
		canvas_->setFontSize(size);
	});
	connect(filledCheck_, &QCheckBox::toggled, this, [this](bool filled) { canvas_->setFilled(filled); });
	connect(clearLayerButton_, &QPushButton::clicked, this, [this] { canvas_->clearSelectedLayer(); });
	connect(clearCanvasButton_, &QPushButton::clicked, this, [this] { canvas_->clearCanvas(); });
	connect(layerList_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (refreshingLayers_ || row < 0 || !layerList_->item(row))
			return;
		canvas_->setSelectedLayer(layerList_->item(row)->data(Qt::UserRole).toString());
	});
	connect(addLayerButton_, &QPushButton::clicked, this, [this] {
		bool accepted = false;
		const QString name = QInputDialog::getText(this, QStringLiteral("Add layer"),
			QStringLiteral("Layer name"), QLineEdit::Normal, QStringLiteral("New layer"), &accepted);
		if (accepted)
			canvas_->addLayer(name);
	});
	connect(deleteLayerButton_, &QPushButton::clicked, this, [this] { canvas_->deleteSelectedLayer(); });
	connect(renameLayerButton_, &QPushButton::clicked, this, [this] {
		QString currentName;
		for (const CanvasLayer& layer : canvas_->layers()) {
			if (layer.id == canvas_->selectedLayerId())
				currentName = layer.name;
		}
		bool accepted = false;
		const QString name = QInputDialog::getText(this, QStringLiteral("Rename layer"),
			QStringLiteral("Layer name"), QLineEdit::Normal, currentName, &accepted);
		if (accepted)
			canvas_->renameSelectedLayer(name);
	});
	connect(visibilityLayerButton_, &QPushButton::clicked, this, [this] {
		for (const CanvasLayer& layer : canvas_->layers()) {
			if (layer.id == canvas_->selectedLayerId())
				canvas_->setSelectedLayerVisible(!layer.visible);
		}
	});
	connect(lockLayerButton_, &QPushButton::clicked, this, [this] {
		for (const CanvasLayer& layer : canvas_->layers()) {
			if (layer.id == canvas_->selectedLayerId())
				canvas_->setSelectedLayerLocked(!layer.locked);
		}
	});
	connect(layerUpButton_, &QPushButton::clicked, this, [this] { canvas_->moveSelectedLayer(1); });
	connect(layerDownButton_, &QPushButton::clicked, this, [this] { canvas_->moveSelectedLayer(-1); });
	connect(sendGuessButton_, &QPushButton::clicked, this, [this] { sendGuess(); });
	connect(guessEdit_, &QLineEdit::returnPressed, this, [this] { sendGuess(); });
	connect(sendChatButton_, &QPushButton::clicked, this, [this] { sendRoomChat(); });
	connect(roomChatEdit_, &QLineEdit::returnPressed, this, [this] { sendRoomChat(); });
	connect(voiceButton_, &QPushButton::toggled, this, [this](bool enabled) {
		if (enabled)
			startVoiceInput();
		else
			stopVoiceInput();
	});
	connect(promoteToPlayerButton_, &QPushButton::clicked, this, [this] {
		if (!pendingRoleChangeRequestId_.isEmpty())
			return;
		pendingRoleChangeRequestId_ = nextRequestId();
		sendGameMessage(makeMessage(QStringLiteral("ChangeRole"),
			{{QStringLiteral("requestId"), pendingRoleChangeRequestId_},
			 {QStringLiteral("spectator"), queuedForPlayer_ || !isSpectator_}}));
		promoteToPlayerButton_->setEnabled(false);
	});
	connect(leaveButton, &QPushButton::clicked, this, [this] { leaveRoom(); });
	updateLayerList(QJsonArray{CanvasLayer{QStringLiteral("layer-default"), QStringLiteral("Background"), 0, true, false}.toJson()});
	updateRoundControls();
	return page;
}

void MainWindow::updateLayerList(const QJsonArray&)
{
	if (!layerList_ || !canvas_)
		return;
	refreshingLayers_ = true;
	layerList_->clear();
	int selectedRow = -1;
	const QList<CanvasLayer> currentLayers = canvas_->layers();
	for (int index = 0; index < currentLayers.size(); ++index) {
		const CanvasLayer& layer = currentLayers.at(index);
		QString label = layer.name;
		if (!layer.visible)
			label += QStringLiteral(" · hidden");
		if (layer.locked)
			label += QStringLiteral(" · locked");
		auto* item = new QListWidgetItem(label, layerList_);
		item->setData(Qt::UserRole, layer.id);
		if (layer.id == canvas_->selectedLayerId())
			selectedRow = index;
	}
	if (selectedRow < 0 && layerList_->count() > 0)
		selectedRow = 0;
	if (selectedRow >= 0)
		layerList_->setCurrentRow(selectedRow);
	refreshingLayers_ = false;
	if (selectedRow >= 0)
		canvas_->setSelectedLayer(layerList_->item(selectedRow)->data(Qt::UserRole).toString());
	for (const CanvasLayer& layer : currentLayers) {
		if (layer.id == canvas_->selectedLayerId()) {
			visibilityLayerButton_->setText(layer.visible ? QStringLiteral("Hide") : QStringLiteral("Show"));
			lockLayerButton_->setText(layer.locked ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
			deleteLayerButton_->setEnabled(roundActive_ && currentDrawer_ == username_
				&& currentLayers.size() > 1 && !layer.locked);
		}
	}
	updateRoundControls();
}

void MainWindow::chooseTextAt(const QPointF& position)
{
	bool accepted = false;
	const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("Add text"),
		QStringLiteral("Text to place on the canvas"), QString(), &accepted);
	if (accepted && !text.trimmed().isEmpty())
		canvas_->addTextAt(position, text);
}

void MainWindow::chooseColor()
{
	const QColor initial(colorButton_->property("canvasColor").toString().isEmpty()
		? QStringLiteral("#202124") : colorButton_->property("canvasColor").toString());
	const QColor color = QColorDialog::getColor(initial, this, QStringLiteral("Choose drawing color"));
	if (!color.isValid())
		return;
	const QString colorName = color.name(QColor::HexRgb);
	canvas_->setCurrentColor(colorName);
	colorButton_->setProperty("canvasColor", colorName);
	colorButton_->setStyleSheet(QStringLiteral("background-color: %1; color: %2")
		.arg(colorName, color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
}

void MainWindow::connectMaster()
{
	if (masterSocket_.state() == QAbstractSocket::UnconnectedState)
		masterSocket_.connectToHost(masterHost_, masterPort_);
}

void MainWindow::readMaster()
{
	QString error;
	const QList<QJsonObject> messages = takeMessages(&masterBuffer_, masterSocket_.readAll(), &error);
	if (!error.isEmpty()) {
		connectionStatusLabel_->setText(error);
		masterSocket_.disconnectFromHost();
		return;
	}
	for (const QJsonObject& message : messages)
		handleMasterMessage(message);
}

void MainWindow::readGameServer()
{
	QString error;
	const QList<QJsonObject> messages = takeMessages(&gameBuffer_, gameSocket_.readAll(), &error);
	if (!error.isEmpty()) {
		appendGameMessage(error);
		gameSocket_.disconnectFromHost();
		return;
	}
	for (const QJsonObject& message : messages)
		handleGameMessage(message);
}

void MainWindow::submitAuthentication()
{
	if (masterSocket_.state() != QAbstractSocket::ConnectedState) {
		loginStatusLabel_->setText(QStringLiteral("Waiting for the master service connection…"));
		connectMaster();
		return;
	}
	const QString username = usernameEdit_->text().trimmed();
	const QString password = passwordEdit_->text();
	if (username.isEmpty() || password.isEmpty()) {
		loginStatusLabel_->setText(QStringLiteral("Enter a username and password."));
		return;
	}
	const bool create = createAccountCheck_->isChecked();
	if (create && displayNameEdit_->text().trimmed().isEmpty()) {
		loginStatusLabel_->setText(QStringLiteral("Enter a display name for your new account."));
		return;
	}
	const QString type = create ? QStringLiteral("CreateAccount") : QStringLiteral("Login");
	QJsonObject fields{{QStringLiteral("requestId"), nextRequestId()},
					   {QStringLiteral("username"), username},
					   {QStringLiteral("password"), password}};
	if (create)
		fields.insert(QStringLiteral("displayName"), displayNameEdit_->text().trimmed());
	pendingAuthRequestId_ = fields.value(QStringLiteral("requestId")).toString();
	loginStatusLabel_->setText(QStringLiteral("Contacting master…"));
	sendMessage(&masterSocket_, makeMessage(type, fields));
}

void MainWindow::handleMasterMessage(const QJsonObject& message)
{
	const QString type = message.value(QStringLiteral("type")).toString();
	if (type == QStringLiteral("SessionReplaced")) {
		authToken_.clear();
		inRoom_ = false;
		leavingRoom_ = false;
		roomRefreshTimer_.stop();
		gameSocket_.abort();
		pages_->setCurrentWidget(loginPage_);
		loginStatusLabel_->setText(QStringLiteral("This account signed in from another connection."));
		return;
	}
	if (type == QStringLiteral("AuthResult")) {
		if (message.value(QStringLiteral("requestId")).toString() != pendingAuthRequestId_)
			return;
		if (!message.value(QStringLiteral("ok")).toBool()) {
			loginStatusLabel_->setText(message.value(QStringLiteral("error")).toString());
			return;
		}
		authToken_ = message.value(QStringLiteral("authToken")).toString();
		const PlayerProfile profile = PlayerProfile::fromJson(message.value(QStringLiteral("profile")).toObject());
		username_ = profile.username;
		if (nicknameEdit_)
			nicknameEdit_->setText(profile.displayName);
		if (auto* account = lobbyPage_->findChild<QLabel*>(QStringLiteral("accountLabel")))
			account->setText(QStringLiteral("Signed in as %1 · %2 points")
								 .arg(profile.displayName).arg(profile.totalScore));
		if (countryCombo_) {
			const int countryIndex = countryCombo_->findData(profile.countryCode);
			countryCombo_->setCurrentIndex(countryIndex >= 0 ? countryIndex : 0);
		}
		pages_->setCurrentWidget(lobbyPage_);
		roomRefreshTimer_.start();
		requestRooms();
		requestLeaderboards();
		return;
	}
	if (type == QStringLiteral("RoomList")) {
		populateRooms(message.value(QStringLiteral("rooms")).toArray());
	} else if (type == QStringLiteral("LeaderboardResult")) {
		const bool drawingPoints = message.value(QStringLiteral("track")).toString() == QStringLiteral("drawing");
		populateLeaderboard(drawingPoints ? drawingLeaderboardTable_ : guessingLeaderboardTable_,
			message.value(QStringLiteral("players")).toArray(), drawingPoints);
	} else if (type == QStringLiteral("ProfileUpdated")) {
		const PlayerProfile profile = PlayerProfile::fromJson(message.value(QStringLiteral("profile")).toObject());
		if (countryCombo_) {
			const int countryIndex = countryCombo_->findData(profile.countryCode);
			countryCombo_->setCurrentIndex(countryIndex >= 0 ? countryIndex : 0);
		}
		if (nicknameEdit_)
			nicknameEdit_->setText(profile.displayName);
		if (currentPasswordEdit_)
			currentPasswordEdit_->clear();
		if (newPasswordEdit_)
			newPasswordEdit_->clear();
		if (profileStatusLabel_)
			profileStatusLabel_->setText(QStringLiteral("Profile saved."));
		if (auto* account = lobbyPage_->findChild<QLabel*>(QStringLiteral("accountLabel")))
			account->setText(QStringLiteral("Signed in as %1 · %2 points")
				.arg(profile.displayName).arg(profile.totalScore));
		requestLeaderboards();
	} else if (type == QStringLiteral("Error")) {
		const QString text = message.value(QStringLiteral("message")).toString();
		if (pages_->currentWidget() == loginPage_)
			loginStatusLabel_->setText(text);
		else
			connectionStatusLabel_->setText(text);
		if (profileStatusLabel_)
			profileStatusLabel_->setText(text);
	}
}

void MainWindow::requestLeaderboards()
{
	if (authToken_.isEmpty() || masterSocket_.state() != QAbstractSocket::ConnectedState)
		return;
	for (const QString& track : {QStringLiteral("guessing"), QStringLiteral("drawing")}) {
		sendMessage(&masterSocket_, makeMessage(QStringLiteral("LeaderboardRequest"),
			{{QStringLiteral("requestId"), nextRequestId()}, {QStringLiteral("authToken"), authToken_},
			 {QStringLiteral("track"), track}}));
	}
}

void MainWindow::populateLeaderboard(QTableWidget* table, const QJsonArray& players, bool drawingPoints)
{
	if (!table)
		return;
	table->setRowCount(players.size());
	for (int row = 0; row < players.size(); ++row) {
		const PlayerProfile profile = PlayerProfile::fromJson(players.at(row).toObject());
		const int rank = drawingPoints ? profile.drawingLeaderboardRank : profile.guessingLeaderboardRank;
		QString flag = QStringLiteral("🏳");
		if (profile.countryCode.size() == 2 && profile.countryCode[0].isLetter() && profile.countryCode[1].isLetter()) {
			const char32_t flagCharacters[] = {
				static_cast<char32_t>(profile.countryCode[0].toUpper().unicode() - QLatin1Char('A').unicode() + 0x1F1E6),
				static_cast<char32_t>(profile.countryCode[1].toUpper().unicode() - QLatin1Char('A').unicode() + 0x1F1E6)};
			flag = QString::fromUcs4(flagCharacters, 2);
			flag.append(QStringLiteral(" %1").arg(profile.countryCode));
		}
		const int points = drawingPoints ? profile.drawingScore : profile.guessingScore;
		table->setItem(row, 0, new QTableWidgetItem(QString::number(rank)));
		table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1 (@%2)").arg(profile.displayName, profile.username)));
		table->setItem(row, 2, new QTableWidgetItem(flag));
		table->item(row, 2)->setToolTip(profile.countryCode.isEmpty() ? QStringLiteral("Country not set") : profile.countryCode);
		table->setItem(row, 3, new QTableWidgetItem(QString::number(points)));
	}
}

void MainWindow::saveProfile()
{
	if (authToken_.isEmpty() || masterSocket_.state() != QAbstractSocket::ConnectedState)
		return;
	if (currentPasswordEdit_->text().isEmpty() != newPasswordEdit_->text().isEmpty()) {
		profileStatusLabel_->setText(QStringLiteral("Enter both current and new passwords to change your password."));
		return;
	}
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("UpdateProfile"),
		{{QStringLiteral("requestId"), nextRequestId()}, {QStringLiteral("authToken"), authToken_},
		 {QStringLiteral("displayName"), nicknameEdit_->text()},
		 {QStringLiteral("countryCode"), countryCombo_->currentData().toString()},
		 {QStringLiteral("currentPassword"), currentPasswordEdit_->text()},
		 {QStringLiteral("newPassword"), newPasswordEdit_->text()}}));
	profileStatusLabel_->setText(QStringLiteral("Saving profile…"));
}

void MainWindow::handleGameMessage(const QJsonObject& message)
{
	const QString type = message.value(QStringLiteral("type")).toString();
	if (type == QStringLiteral("Pong")) {
		if (message.value(QStringLiteral("requestId")).toString() == pendingActivePingRequestId_) {
			activePingMs_ = static_cast<int>(activePingElapsed_.elapsed());
			pendingActivePingRequestId_.clear();
			activeLatencyIndicator_->setPing(activePingMs_);
		}
		return;
	}
	if (type == QStringLiteral("VoiceFrame")) {
		handleVoiceFrame(message);
		return;
	}
	if (type == QStringLiteral("JoinResult")) {
		if (message.value(QStringLiteral("requestId")).toString() != pendingJoinRequestId_)
			return;
		if (!message.value(QStringLiteral("ok")).toBool()) {
			connectionStatusLabel_->setText(message.value(QStringLiteral("error")).toString());
			gameSocket_.disconnectFromHost();
			return;
		}
		stopVoiceInput();
		clearVoiceOutputs();
		inRoom_ = true;
		leavingRoom_ = false;
		activePingMs_ = -1;
		pendingActivePingRequestId_.clear();
		activeLatencyIndicator_->setPing(-1);
		activePingTimer_.start();
		sendActivePing();
		isSpectator_ = message.value(QStringLiteral("spectator")).toBool(false);
		queuedForPlayer_ = message.value(QStringLiteral("queuedForPlayer")).toBool(false);
		currentDrawer_.clear();
		const QJsonObject room = message.value(QStringLiteral("room")).toObject();
		joiningRoom_.players = room.value(QStringLiteral("players")).toInt();
		joiningRoom_.spectators = room.value(QStringLiteral("spectators")).toInt();
		joiningRoom_.maxPlayers = room.value(QStringLiteral("maxPlayers")).toInt(joiningRoom_.maxPlayers);
		gameRoomLabel_->setText(QStringLiteral("Room: %1").arg(joiningRoom_.roomName));
		canvas_->setScene(message.value(QStringLiteral("layers")).toArray(),
			message.value(QStringLiteral("objects")).toArray());
		pages_->setCurrentWidget(gamePage_);
		updateRoster(message.value(QStringLiteral("players")).toArray(), room);
		updateQueueStatus(message.value(QStringLiteral("queue")).toObject());
		roundStatusLabel_->setText(QStringLiteral("Waiting for players"));
		appendGameMessage(QStringLiteral("Joined %1 as %2")
			.arg(joiningRoom_.roomName, isSpectator_ ? QStringLiteral("spectator") : QStringLiteral("player")));
		if (message.value(QStringLiteral("roundActive")).toBool()) {
			currentDrawer_ = message.value(QStringLiteral("drawerUsername")).toString();
			secondsRemaining_ = message.value(QStringLiteral("secondsRemaining")).toInt();
			roundActive_ = true;
			updateRoster(message.value(QStringLiteral("players")).toArray(), {});
			roundStatusLabel_->setText(QStringLiteral("%1Round in progress · drawing: %2")
				.arg(isSpectator_ ? QStringLiteral("Spectating · ") : QString(), currentDrawer_));
		}
		updateRoundControls();
		return;
	}
	if (type == QStringLiteral("CanvasState")) {
		canvas_->setScene(message.value(QStringLiteral("layers")).toArray(),
			message.value(QStringLiteral("objects")).toArray());
	} else if (type == QStringLiteral("CanvasOperation")) {
		canvas_->applyOperation(message.value(QStringLiteral("operation")).toObject());
	} else if (type == QStringLiteral("RoomRoster")) {
		updateRoster(message.value(QStringLiteral("players")).toArray(), message.value(QStringLiteral("room")).toObject());
	} else if (type == QStringLiteral("QueueStatus")) {
		updateQueueStatus(message);
	} else if (type == QStringLiteral("RoleChanged")) {
		isSpectator_ = message.value(QStringLiteral("spectator")).toBool(true);
		queuedForPlayer_ = false;
		updateQueueStatus({});
		appendGameMessage(QStringLiteral("A player slot opened; you joined the players"));
		updateRoundControls();
	} else if (type == QStringLiteral("RoleChangeResult")) {
		if (message.value(QStringLiteral("requestId")).toString() != pendingRoleChangeRequestId_)
			return;
		pendingRoleChangeRequestId_.clear();
		if (!message.value(QStringLiteral("ok")).toBool()) {
			appendGameMessage(message.value(QStringLiteral("error")).toString());
			updateRoundControls();
			return;
		}
		isSpectator_ = message.value(QStringLiteral("spectator")).toBool(false);
		if (message.value(QStringLiteral("queued")).toBool()) {
			queuedForPlayer_ = true;
			updateQueueStatus(message.value(QStringLiteral("queue")).toObject());
			appendGameMessage(QStringLiteral("You are waiting for a player slot"));
		} else {
			queuedForPlayer_ = false;
			updateQueueStatus({});
			appendGameMessage(isSpectator_ ? QStringLiteral("You are now spectating")
				: QStringLiteral("You joined the players"));
		}
		if (roundActive_)
			roundStatusLabel_->setText(QStringLiteral("Round in progress · drawing: %1").arg(currentDrawer_));
		updateRoundControls();
	} else if (type == QStringLiteral("RoundStarted")) {
		roundActive_ = true;
		currentDrawer_ = message.value(QStringLiteral("drawerUsername")).toString();
		updateRoster(roomRoster_, {});
		secondsRemaining_ = message.value(QStringLiteral("secondsRemaining")).toInt();
		wordLabel_->setText(username_ == currentDrawer_ ? QStringLiteral("Your word: …") : QStringLiteral("Your word: —"));
		roundStatusLabel_->setText(QStringLiteral("%1Round %2 · %3 is drawing")
			.arg(isSpectator_ ? QStringLiteral("Spectating · ") : QString())
			.arg(message.value(QStringLiteral("round")).toInt()).arg(currentDrawer_));
		updateRoundControls();
	} else if (type == QStringLiteral("WordAssigned")) {
		wordLabel_->setText(QStringLiteral("Your word: %1").arg(message.value(QStringLiteral("word")).toString()));
	} else if (type == QStringLiteral("Countdown")) {
		secondsRemaining_ = message.value(QStringLiteral("secondsRemaining")).toInt();
		timerLabel_->setText(QStringLiteral("%1:%2").arg(secondsRemaining_ / 60, 2, 10, QLatin1Char('0'))
													   .arg(secondsRemaining_ % 60, 2, 10, QLatin1Char('0')));
	} else if (type == QStringLiteral("ClearCanvas")) {
		canvas_->applyOperation({{QStringLiteral("action"), QStringLiteral("clear")}});
	} else if (type == QStringLiteral("RoundEnded")) {
		roundActive_ = false;
		currentDrawer_.clear();
		updateRoster(roomRoster_, {});
		wordLabel_->setText(QStringLiteral("Answer: %1").arg(message.value(QStringLiteral("word")).toString()));
		roundStatusLabel_->setText(QStringLiteral("Round ended"));
		appendGameMessage(QStringLiteral("The word was %1").arg(message.value(QStringLiteral("word")).toString()));
		updateRoundControls();
	} else if (type == QStringLiteral("RoundPaused")) {
		roundActive_ = false;
		updateRoster(roomRoster_, {});
		roundStatusLabel_->setText(message.value(QStringLiteral("message")).toString());
		updateRoundControls();
	} else if (type == QStringLiteral("GuessCorrect")) {
		appendGameMessage(QStringLiteral("%1 guessed correctly in place %2 (+%3 points)")
			.arg(message.value(QStringLiteral("displayName")).toString())
			.arg(message.value(QStringLiteral("placement")).toInt())
			.arg(message.value(QStringLiteral("points")).toInt()));
	} else if (type == QStringLiteral("ChatMessage")) {
		appendGameMessage(QStringLiteral("%1: %2")
			.arg(message.value(QStringLiteral("displayName")).toString(),
				 message.value(QStringLiteral("text")).toString()));
	} else if (type == QStringLiteral("PlayerLeft")) {
		appendGameMessage(QStringLiteral("%1 left the room").arg(message.value(QStringLiteral("username")).toString()));
	} else if (type == QStringLiteral("Error")) {
		appendGameMessage(message.value(QStringLiteral("message")).toString());
	}
}

void MainWindow::requestRooms()
{
	if (authToken_.isEmpty())
		return;
	if (masterSocket_.state() != QAbstractSocket::ConnectedState) {
		connectMaster();
		return;
	}
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("ListRooms"),
												 {{QStringLiteral("requestId"), nextRequestId()},
												  {QStringLiteral("authToken"), authToken_}}));
}

void MainWindow::populateRooms(const QJsonArray& rooms)
{
	roomLatencyIndicators_.clear();
	roomsTable_->setRowCount(0);
	roomsById_.clear();
	for (const QJsonValue& value : rooms) {
		if (!value.isObject())
			continue;
		const RoomInfo room = RoomInfo::fromJson(value.toObject());
		if (room.roomId.isEmpty())
			continue;
		const int row = roomsTable_->rowCount();
		roomsTable_->insertRow(row);
		roomsById_.insert(room.roomId, room);
		auto* roomNameItem = new QTableWidgetItem(room.roomName);
		roomNameItem->setData(Qt::UserRole, room.roomId);
		roomsTable_->setItem(row, 0, roomNameItem);
		roomsTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1 / %2 · %3 spectating")
															   .arg(room.players).arg(room.maxPlayers).arg(room.spectators)));
		roomsTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1 sec").arg(room.roundSeconds)));
		auto* latencyIndicator = new LatencyIndicator(roomsTable_);
		latencyIndicator->setPing(roomPingMs_.value(room.roomId, -1));
		roomLatencyIndicators_.insert(room.roomId, latencyIndicator);
		roomsTable_->setCellWidget(row, 3, latencyIndicator);
		roomsTable_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1:%2").arg(room.host).arg(room.port)));
	}
	roomsRefreshElapsed_.start();
	connectionStatusLabel_->setText(QStringLiteral("%1 room(s) listed").arg(roomsTable_->rowCount()));
	updateRoomsRefreshStatus();
	joinRoomButton_->setEnabled(roomsTable_->currentRow() >= 0);
	probeRooms(rooms);
}

void MainWindow::probeRooms(const QJsonArray& rooms)
{
	for (const QJsonValue& value : rooms) {
		if (!value.isObject())
			continue;
		const RoomInfo room = RoomInfo::fromJson(value.toObject());
		if (room.roomId.isEmpty() || room.host.isEmpty() || room.port == 0
			|| roomPingSockets_.contains(room.roomId))
			continue;

		auto* socket = new QTcpSocket(this);
		RoomPingProbe probe;
		probe.roomId = room.roomId;
		probe.requestId = nextRequestId();
		roomPingProbes_.insert(socket, probe);
		roomPingSockets_.insert(room.roomId, socket);
		connect(socket, &QTcpSocket::connected, this, [this, socket] {
			auto it = roomPingProbes_.find(socket);
			if (it == roomPingProbes_.end())
				return;
			it->elapsed.start();
			if (!sendMessage(socket, makeMessage(QStringLiteral("Ping"),
				{{QStringLiteral("requestId"), it->requestId}})))
				finishRoomPingProbe(socket);
		});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
			auto it = roomPingProbes_.find(socket);
			if (it == roomPingProbes_.end())
				return;
			QString error;
			const QList<QJsonObject> messages = takeMessages(&it->buffer, socket->readAll(), &error);
			if (!error.isEmpty()) {
				finishRoomPingProbe(socket);
				return;
			}
			for (const QJsonObject& message : messages) {
				if (message.value(QStringLiteral("type")).toString() == QStringLiteral("Pong")
					&& message.value(QStringLiteral("requestId")).toString() == it->requestId) {
					finishRoomPingProbe(socket, static_cast<int>(it->elapsed.elapsed()));
					return;
				}
			}
		});
		connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
			finishRoomPingProbe(socket);
		});
		QTimer::singleShot(3000, socket, [this, socket] {
			finishRoomPingProbe(socket);
		});
		socket->connectToHost(room.host, room.port);
	}
}

void MainWindow::finishRoomPingProbe(QTcpSocket* socket, int pingMs)
{
	auto it = roomPingProbes_.find(socket);
	if (it == roomPingProbes_.end())
		return;
	const QString roomId = it->roomId;
	if (roomPingSockets_.value(roomId) == socket)
		roomPingSockets_.remove(roomId);
	roomPingProbes_.erase(it);
	if (pingMs >= 0)
		roomPingMs_.insert(roomId, pingMs);
	else
		roomPingMs_.remove(roomId);
	if (roomLatencyIndicators_.contains(roomId))
		roomLatencyIndicators_.value(roomId)->setPing(pingMs);
	socket->abort();
	socket->deleteLater();
}

void MainWindow::sendActivePing()
{
	if (!inRoom_ || gameSocket_.state() != QAbstractSocket::ConnectedState)
		return;
	if (!pendingActivePingRequestId_.isEmpty()) {
		if (activePingElapsed_.elapsed() < 5000)
			return;
		pendingActivePingRequestId_.clear();
		activePingMs_ = -1;
		activeLatencyIndicator_->setPing(-1);
	}
	pendingActivePingRequestId_ = nextRequestId();
	activePingElapsed_.start();
	sendGameMessage(makeMessage(QStringLiteral("Ping"),
		{{QStringLiteral("requestId"), pendingActivePingRequestId_}}));
}

void MainWindow::updateRoomsRefreshStatus()
{
	if (!roomsRefreshElapsed_.isValid() || !connectionStatusLabel_
		|| !connectionStatusLabel_->text().contains(QStringLiteral("room(s) listed")))
		return;
	const qint64 secondsAgo = roomsRefreshElapsed_.elapsed() / 1000;
	connectionStatusLabel_->setText(QStringLiteral("%1 room(s) listed · refreshed %2 second%3 ago")
		.arg(roomsTable_->rowCount())
		.arg(secondsAgo)
		.arg(secondsAgo == 1 ? QString() : QStringLiteral("s")));
}

void MainWindow::joinSelectedRoom()
{
	const int row = roomsTable_->currentRow();
	if (row < 0 || !roomsTable_->item(row, 0))
		return;
	const QString roomId = roomsTable_->item(row, 0)->data(Qt::UserRole).toString();
	joiningRoom_ = roomsById_.value(roomId);
	if (joiningRoom_.roomId.isEmpty() || joiningRoom_.host.isEmpty() || joiningRoom_.port == 0) {
		connectionStatusLabel_->setText(QStringLiteral("Selected room has an invalid server address"));
		return;
	}
	pendingRoomId_ = joiningRoom_.roomId;
	pendingJoinAsSpectator_ = joinRoleCombo_->currentData().toBool();
	pendingJoinRequestId_.clear();
	pendingRoleChangeRequestId_.clear();
	gameBuffer_.clear();
	inRoom_ = false;
	activePingTimer_.stop();
	pendingActivePingRequestId_.clear();
	activePingMs_ = -1;
	activeLatencyIndicator_->setPing(-1);
	isSpectator_ = false;
	roundActive_ = false;
	leavingRoom_ = false;
	connectionStatusLabel_->setText(QStringLiteral("Connecting to %1…").arg(joiningRoom_.roomName));
	gameSocket_.abort();
	gameSocket_.connectToHost(joiningRoom_.host, joiningRoom_.port);
}

void MainWindow::leaveRoom()
{
	if (gameSocket_.state() == QAbstractSocket::ConnectedState) {
		leavingRoom_ = true;
		sendGameMessage(makeMessage(QStringLiteral("LeaveRoom")));
		gameSocket_.disconnectFromHost();
	}
	stopVoiceInput();
	clearVoiceOutputs();
	inRoom_ = false;
	activePingTimer_.stop();
	pendingActivePingRequestId_.clear();
	activePingMs_ = -1;
	activeLatencyIndicator_->setPing(-1);
	isSpectator_ = false;
	pendingRoleChangeRequestId_.clear();
	roundActive_ = false;
	pages_->setCurrentWidget(lobbyPage_);
	requestRooms();
}

void MainWindow::sendGameMessage(const QJsonObject& message)
{
	sendMessage(&gameSocket_, message);
}

void MainWindow::sendGuess()
{
	const QString guess = guessEdit_->text().trimmed();
	if (guess.isEmpty())
		return;
	sendGameMessage(makeMessage(QStringLiteral("Guess"), {{QStringLiteral("text"), guess}}));
	guessEdit_->clear();
}

void MainWindow::sendRoomChat()
{
	const QString text = roomChatEdit_->text().trimmed();
	if (!inRoom_ || text.isEmpty() || (roundActive_ && currentDrawer_ == username_))
		return;
	sendGameMessage(makeMessage(QStringLiteral("RoomChat"),
		{{QStringLiteral("requestId"), nextRequestId()}, {QStringLiteral("text"), text.left(300)}}));
	roomChatEdit_->clear();
}

void MainWindow::startVoiceInput()
{
	if (!inRoom_ || gameSocket_.state() != QAbstractSocket::ConnectedState
		|| (roundActive_ && currentDrawer_ == username_)) {
		stopVoiceInput();
		return;
	}
	const QAudioDevice input = QMediaDevices::defaultAudioInput();
	if (input.isNull()) {
		stopVoiceInput();
		voiceStatusLabel_->setText(QStringLiteral("No microphone device"));
		return;
	}
	const QAudioFormat format = input.preferredFormat();
	if (!usableAudioFormat(format) || !input.isFormatSupported(format)) {
		stopVoiceInput();
		voiceStatusLabel_->setText(QStringLiteral("Unsupported microphone format"));
		return;
	}
	voiceInputFormat_ = format;
	voiceInputBuffer_.clear();
	voiceCaptureBuffer_.clear();
	voiceResampleInput_.clear();
	voiceResamplePosition_ = 0.0;
	voiceInput_ = new QAudioSource(input, format, this);
	voiceInput_->setBufferSize(qMax(6400, format.sampleRate() * format.channelCount()
		* audioBytesPerSample(format.sampleFormat()) / 5));
	voiceInputDevice_ = voiceInput_->start();
	if (!voiceInputDevice_ || voiceInput_->error() != QAudio::NoError) {
		stopVoiceInput();
		voiceStatusLabel_->setText(QStringLiteral("Could not start microphone"));
		return;
	}
	connect(voiceInputDevice_, &QIODevice::readyRead, this, [this] {
		if (!voiceInputDevice_ || !voiceInput_)
			return;
		voiceInputBuffer_.append(voiceInputDevice_->readAll());
		const int bytesPerSample = audioBytesPerSample(voiceInputFormat_.sampleFormat());
		const int bytesPerFrame = bytesPerSample * voiceInputFormat_.channelCount();
		if (bytesPerFrame <= 0)
			return;
		const int completeBytes = voiceInputBuffer_.size() / bytesPerFrame * bytesPerFrame;
		const int inputFrames = completeBytes / bytesPerFrame;
		for (int frame = 0; frame < inputFrames; ++frame) {
			const char* frameData = voiceInputBuffer_.constData() + frame * bytesPerFrame;
			float monoSample = 0.0f;
			for (int channel = 0; channel < voiceInputFormat_.channelCount(); ++channel)
				monoSample += decodeAudioSample(frameData + channel * bytesPerSample, voiceInputFormat_);
			voiceResampleInput_.append(monoSample / voiceInputFormat_.channelCount());
		}
		voiceInputBuffer_.remove(0, completeBytes);

		const double inputSamplesPerOutputSample = voiceInputFormat_.sampleRate() / 16000.0;
		while (voiceResamplePosition_ + 1.0 < voiceResampleInput_.size()) {
			const int first = static_cast<int>(voiceResamplePosition_);
			const float fraction = static_cast<float>(voiceResamplePosition_ - first);
			const float firstSample = voiceResampleInput_.at(first);
			const float secondSample = voiceResampleInput_.at(first + 1);
			const float sample = firstSample + (secondSample - firstSample) * fraction;
			const qint16 pcmSample = static_cast<qint16>(std::clamp(qRound(sample * 32767.0f), -32768, 32767));
			char pcmBytes[2];
			qToLittleEndian<qint16>(pcmSample, reinterpret_cast<uchar*>(pcmBytes));
			voiceCaptureBuffer_.append(pcmBytes, sizeof(pcmBytes));
			voiceResamplePosition_ += inputSamplesPerOutputSample;
		}
		const int consumedSamples = qMin(static_cast<int>(voiceResamplePosition_), voiceResampleInput_.size() - 1);
		if (consumedSamples > 0) {
			voiceResampleInput_.remove(0, consumedSamples);
			voiceResamplePosition_ -= consumedSamples;
		}

		while (voiceCaptureBuffer_.size() >= 640) {
			const QByteArray frame = voiceCaptureBuffer_.left(640);
			voiceCaptureBuffer_.remove(0, 640);
			sendGameMessage(makeMessage(QStringLiteral("VoiceFrame"),
				{{QStringLiteral("sampleRate"), 16000},
				 {QStringLiteral("audio"), QString::fromLatin1(frame.toBase64())}}));
		}
	});
	connect(voiceInput_, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
		if (voiceInput_ && state == QAudio::StoppedState && voiceInput_->error() != QAudio::NoError) {
			stopVoiceInput();
			voiceStatusLabel_->setText(QStringLiteral("Microphone stopped unexpectedly"));
		}
	});
	voiceButton_->setText(QStringLiteral("Disable microphone"));
	voiceStatusLabel_->setText(QStringLiteral("Microphone on"));
}

void MainWindow::stopVoiceInput()
{
	if (voiceInput_) {
		voiceInput_->stop();
		voiceInput_->deleteLater();
		voiceInput_ = nullptr;
	}
	if (voiceInputDevice_)
		disconnect(voiceInputDevice_, nullptr, this, nullptr);
	voiceInputDevice_ = nullptr;
	voiceCaptureBuffer_.clear();
	voiceInputBuffer_.clear();
	voiceResampleInput_.clear();
	voiceResamplePosition_ = 0.0;
	voiceInputFormat_ = QAudioFormat();
	if (voiceButton_) {
		const QSignalBlocker blocker(voiceButton_);
		voiceButton_->setChecked(false);
		voiceButton_->setText(QStringLiteral("Enable microphone"));
	}
	if (voiceStatusLabel_)
		voiceStatusLabel_->setText(inRoom_ ? QStringLiteral("Microphone off") : QString());
}

void MainWindow::clearVoiceOutputs()
{
	voiceOutputFlushTimer_.stop();
	for (QAudioSink* sink : voiceOutputs_) {
		sink->stop();
		sink->deleteLater();
	}
	voiceOutputs_.clear();
	voiceOutputDevices_.clear();
	voiceOutputFormats_.clear();
	voiceOutputPending_.clear();
}

void MainWindow::flushVoiceOutputs()
{
	for (auto it = voiceOutputPending_.begin(); it != voiceOutputPending_.end(); ++it) {
		QIODevice* device = voiceOutputDevices_.value(it.key());
		if (!device || it.value().isEmpty())
			continue;
		const qint64 written = device->write(it.value().constData(), it.value().size());
		if (written > 0)
			it.value().remove(0, written);
	}
}

void MainWindow::handleVoiceFrame(const QJsonObject& message)
{
	const QString username = message.value(QStringLiteral("username")).toString();
	const QByteArray encodedAudio = message.value(QStringLiteral("audio")).toString().toLatin1();
	if (encodedAudio.size() != 856)
		return;
	const QByteArray audio = QByteArray::fromBase64(encodedAudio);
	if (!inRoom_ || username.isEmpty() || username == username_
		|| message.value(QStringLiteral("sampleRate")).toInt() != 16000
		|| audio.size() != 640 || audio.toBase64() != encodedAudio)
		return;
	QIODevice* device = voiceOutputDevices_.value(username);
	if (!device) {
		const QAudioDevice output = QMediaDevices::defaultAudioOutput();
		if (output.isNull()) {
			voiceStatusLabel_->setText(QStringLiteral("No audio output device"));
			return;
		}
		const QAudioFormat format = output.preferredFormat();
		if (!usableAudioFormat(format) || !output.isFormatSupported(format)) {
			voiceStatusLabel_->setText(QStringLiteral("Unsupported audio output format"));
			return;
		}
		auto* sink = new QAudioSink(output, format, this);
		const int outputBytesPerSecond = format.sampleRate() * format.channelCount()
			* audioBytesPerSample(format.sampleFormat());
		// A server frame is 20 ms, but its converted device-format size varies.
		// Keep enough room for several complete frames (100 ms) rather than
		// assuming the network frame's 640-byte PCM size matches the device buffer.
		sink->setBufferSize(qMax(6400, outputBytesPerSecond / 10));
		device = sink->start();
		if (!device || sink->error() != QAudio::NoError) {
			sink->stop();
			sink->deleteLater();
			voiceStatusLabel_->setText(QStringLiteral("Could not start audio playback"));
			return;
		}
		voiceOutputs_.insert(username, sink);
		voiceOutputDevices_.insert(username, device);
		voiceOutputFormats_.insert(username, format);
		voiceOutputPending_.insert(username, {});
		voiceOutputFlushTimer_.start();
	}
	voiceOutputPending_[username].append(convertServerFrameForOutput(audio, voiceOutputFormats_.value(username)));
	flushVoiceOutputs();
}

void MainWindow::appendGameMessage(const QString& message)
{
	chatList_->addItem(message);
	while (chatList_->count() > 250)
		delete chatList_->takeItem(0);
	chatList_->scrollToBottom();
}

void MainWindow::updateRoundControls()
{
	const bool isDrawer = roundActive_ && currentDrawer_ == username_ && !isSpectator_;
	bool selectedLayerLocked = false;
	for (const CanvasLayer& layer : canvas_->layers()) {
		if (layer.id == canvas_->selectedLayerId())
			selectedLayerLocked = layer.locked;
	}
	canvas_->setDrawingAllowed(isDrawer);
	promoteToPlayerButton_->setVisible(inRoom_);
	promoteToPlayerButton_->setText(queuedForPlayer_ ? QStringLiteral("Cancel Wait")
		: isSpectator_ ? QStringLiteral("Join as Player") : QStringLiteral("Switch to Spectator"));
	promoteToPlayerButton_->setEnabled(inRoom_ && pendingRoleChangeRequestId_.isEmpty());
	clearLayerButton_->setEnabled(isDrawer && !selectedLayerLocked);
	clearCanvasButton_->setEnabled(isDrawer);
	toolCombo_->setEnabled(isDrawer);
	colorButton_->setEnabled(isDrawer);
	strokeWidthSpin_->setEnabled(isDrawer);
	fontSizeSpin_->setEnabled(isDrawer);
	filledCheck_->setEnabled(isDrawer);
	addLayerButton_->setEnabled(isDrawer && canvas_->layers().size() < MaximumCanvasLayers);
	renameLayerButton_->setEnabled(isDrawer);
	visibilityLayerButton_->setEnabled(isDrawer);
	lockLayerButton_->setEnabled(isDrawer);
	layerUpButton_->setEnabled(isDrawer);
	layerDownButton_->setEnabled(isDrawer);
	guessEdit_->setEnabled(roundActive_ && !isDrawer && !isSpectator_);
	sendGuessButton_->setEnabled(roundActive_ && !isDrawer && !isSpectator_);
	if (isDrawer && voiceButton_->isChecked())
		stopVoiceInput();
	roomChatEdit_->setEnabled(inRoom_ && !isDrawer);
	roomChatEdit_->setPlaceholderText(isDrawer
		? QStringLiteral("Drawer can listen but not send") : QStringLiteral("Send a room message"));
	sendChatButton_->setEnabled(inRoom_ && !isDrawer);
	voiceButton_->setEnabled(inRoom_ && !isDrawer);
	voiceButton_->setText(isDrawer ? QStringLiteral("Drawer: receive only")
		: voiceButton_->isChecked() ? QStringLiteral("Disable microphone") : QStringLiteral("Enable microphone"));
	if (isDrawer)
		voiceStatusLabel_->setText(QStringLiteral("You can hear room voice chat"));
	else if (!voiceButton_->isChecked())
		voiceStatusLabel_->setText(inRoom_ ? QStringLiteral("Microphone off") : QString());
	if (!roundActive_)
		timerLabel_->setText(QStringLiteral("—:—"));
}

void MainWindow::updateRoster(const QJsonArray& players, const QJsonObject& room)
{
	roomRoster_ = players;
	if (!room.isEmpty()) {
		joiningRoom_.players = room.value(QStringLiteral("players")).toInt(joiningRoom_.players);
		joiningRoom_.spectators = room.value(QStringLiteral("spectators")).toInt(joiningRoom_.spectators);
		joiningRoom_.maxPlayers = room.value(QStringLiteral("maxPlayers")).toInt(joiningRoom_.maxPlayers);
	}
	gameRoomLabel_->setText(QStringLiteral("Room: %1 · Players: %2/%3 · Spectators: %4")
		.arg(joiningRoom_.roomName).arg(joiningRoom_.players).arg(joiningRoom_.maxPlayers)
		.arg(joiningRoom_.spectators));
	rosterList_->clear();
	for (const QJsonValue& value : players) {
		const QJsonObject entry = value.toObject();
		const PlayerProfile profile = PlayerProfile::fromJson(entry.value(QStringLiteral("profile")).toObject());
		const QString role = entry.value(QStringLiteral("spectator")).toBool()
			? QStringLiteral(" · Spectator") : QString();
		const QString drawing = roundActive_ && profile.username == currentDrawer_
			? QStringLiteral(" · Drawing") : QString();
		auto* rosterItem = new QListWidgetItem(QStringLiteral("%1%2%3 (@%4)\nRoom: %5 · Total: %6")
			.arg(profile.displayName, role, drawing, profile.username)
			.arg(entry.value(QStringLiteral("roomScore")).toInt())
			.arg(profile.totalScore), rosterList_);
		const QString globalRank = profile.leaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.leaderboardRank)
			: QStringLiteral("Unranked");
		const QString guessingRank = profile.guessingRank.isEmpty()
			? QStringLiteral("Unrated") : profile.guessingRank;
		const QString drawingRank = profile.drawingRank.isEmpty()
			? QStringLiteral("Unrated") : profile.drawingRank;
		const QString guessingLeaderboardRank = profile.guessingLeaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.guessingLeaderboardRank) : QStringLiteral("Unranked");
		const QString drawingLeaderboardRank = profile.drawingLeaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.drawingLeaderboardRank) : QStringLiteral("Unranked");
		const QString ratio = profile.roundsLost > 0
			? QString::number(static_cast<double>(profile.roundsWon) / static_cast<double>(profile.roundsLost), 'f', 2)
			: (profile.roundsWon > 0 ? QStringLiteral("∞") : QStringLiteral("N/A"));
		rosterItem->setToolTip(QStringLiteral("%1 (@%2)\nGuessing: %3 points · %4 · Rank %5\nDrawing: %6 points · %7 · Rank %8\nCombined: %9 points · Rank %10\nWins: %11\nLosses: %12\nWin/Loss ratio: %13")
			.arg(profile.displayName, profile.username)
			.arg(profile.guessingScore).arg(guessingRank, guessingLeaderboardRank)
			.arg(profile.drawingScore).arg(drawingRank, drawingLeaderboardRank)
			.arg(profile.totalScore).arg(globalRank)
			.arg(profile.roundsWon)
			.arg(profile.roundsLost)
			.arg(ratio));
	}
	updateRoundControls();
}

void MainWindow::updateQueueStatus(const QJsonObject& queue)
{
	queuedForPlayer_ = queue.value(QStringLiteral("queued")).toBool(false);
	if (!queuedForPlayer_) {
		queueStatusLabel_->clear();
		queueStatusLabel_->hide();
		queueProgressBar_->hide();
		updateRoundControls();
		return;
	}
	const int total = qMax(1, queue.value(QStringLiteral("total")).toInt());
	const int position = qBound(1, queue.value(QStringLiteral("position")).toInt(), total);
	const int ahead = queue.value(QStringLiteral("ahead")).toInt(qMax(0, position - 1));
	queueStatusLabel_->setText(QStringLiteral("Waiting for a player slot · %1 ahead · position %2 of %3")
		.arg(ahead).arg(position).arg(total));
	queueStatusLabel_->show();
	queueProgressBar_->setRange(0, total);
	queueProgressBar_->setValue(total - position + 1);
	queueProgressBar_->show();
	updateRoundControls();
}

QString MainWindow::nextRequestId()
{
	return QString::number(requestCounter_++);
}

}
