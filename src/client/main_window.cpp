#include "client/main_window.h"

#include "client/drawing_canvas.h"
#include "shared/protocol.h"

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
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector>

namespace gtd {

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
	roomsTable_ = new QTableWidget(0, 4, page);
	roomsTable_->setHorizontalHeaderLabels({QStringLiteral("Room"), QStringLiteral("Players"),
											 QStringLiteral("Round timer"), QStringLiteral("Server")});
	roomsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	roomsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	roomsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	roomsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
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
	header->addWidget(promoteToPlayerButton_, 0, 4);
	header->addWidget(leaveButton, 0, 5);
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
	sidePanel->addWidget(new QLabel(QStringLiteral("Guesses"), page));
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
	if (type == QStringLiteral("JoinResult")) {
		if (message.value(QStringLiteral("requestId")).toString() != pendingJoinRequestId_)
			return;
		if (!message.value(QStringLiteral("ok")).toBool()) {
			connectionStatusLabel_->setText(message.value(QStringLiteral("error")).toString());
			gameSocket_.disconnectFromHost();
			return;
		}
		inRoom_ = true;
		leavingRoom_ = false;
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
		roomsTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1:%2").arg(room.host).arg(room.port)));
	}
	roomsRefreshElapsed_.start();
	connectionStatusLabel_->setText(QStringLiteral("%1 room(s) listed").arg(roomsTable_->rowCount()));
	updateRoomsRefreshStatus();
	joinRoomButton_->setEnabled(roomsTable_->currentRow() >= 0);
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
	inRoom_ = false;
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
