#include "client/main_window.h"

#include "client/drawing_canvas.h"
#include "shared/protocol.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
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
#include <QPalette>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QHash>

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gtd {

namespace {

QIcon countryFlagIcon(const QString& countryCode)
{
	const QString code = countryCode.toUpper();
	if (code.size() != 2 && code.size() != 6)
		return {};
	for (int index = 0; index < code.size(); ++index) {
		if (code.size() == 6 && index == 2) {
			if (code[index] != QLatin1Char('-'))
				return {};
		} else if (code[index] < QLatin1Char('A') || code[index] > QLatin1Char('Z')) {
			return {};
		}
	}
	return QIcon(QStringLiteral(":/country-flags/%1.png").arg(code.toLower()));
}

QString localizedText(const QString& source, const QString& languageCode)
{
	static const QHash<QString, QHash<QString, QString>> translations{
		{QStringLiteral("es"), {
			{QStringLiteral("Settings"), QStringLiteral("Configuración")},
			{QStringLiteral("Profile settings"), QStringLiteral("Configuración del perfil")},
			{QStringLiteral("App settings"), QStringLiteral("Configuración de la aplicación")},
			{QStringLiteral("Nickname"), QStringLiteral("Apodo")},
			{QStringLiteral("Country / flag"), QStringLiteral("País / bandera")},
			{QStringLiteral("Current password"), QStringLiteral("Contraseña actual")},
			{QStringLiteral("New password"), QStringLiteral("Nueva contraseña")},
			{QStringLiteral("Required to change password"), QStringLiteral("Obligatorio para cambiar la contraseña")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Déjalo en blanco para conservar la contraseña actual")},
			{QStringLiteral("Save profile"), QStringLiteral("Guardar perfil")},
			{QStringLiteral("Language"), QStringLiteral("Idioma")},
			{QStringLiteral("Profile saved."), QStringLiteral("Perfil guardado.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Guardando perfil…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Introduce la contraseña actual y la nueva para cambiarla.")}}},
		{QStringLiteral("fr"), {
			{QStringLiteral("Settings"), QStringLiteral("Paramètres")},
			{QStringLiteral("Profile settings"), QStringLiteral("Paramètres du profil")},
			{QStringLiteral("App settings"), QStringLiteral("Paramètres de l’application")},
			{QStringLiteral("Nickname"), QStringLiteral("Pseudonyme")},
			{QStringLiteral("Country / flag"), QStringLiteral("Pays / drapeau")},
			{QStringLiteral("Current password"), QStringLiteral("Mot de passe actuel")},
			{QStringLiteral("New password"), QStringLiteral("Nouveau mot de passe")},
			{QStringLiteral("Required to change password"), QStringLiteral("Requis pour modifier le mot de passe")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Laisser vide pour conserver le mot de passe actuel")},
			{QStringLiteral("Save profile"), QStringLiteral("Enregistrer le profil")},
			{QStringLiteral("Language"), QStringLiteral("Langue")},
			{QStringLiteral("Profile saved."), QStringLiteral("Profil enregistré.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Enregistrement du profil…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Saisissez les mots de passe actuel et nouveau pour le modifier.")}}},
		{QStringLiteral("de"), {
			{QStringLiteral("Settings"), QStringLiteral("Einstellungen")},
			{QStringLiteral("Profile settings"), QStringLiteral("Profileinstellungen")},
			{QStringLiteral("App settings"), QStringLiteral("App-Einstellungen")},
			{QStringLiteral("Nickname"), QStringLiteral("Spitzname")},
			{QStringLiteral("Country / flag"), QStringLiteral("Land / Flagge")},
			{QStringLiteral("Current password"), QStringLiteral("Aktuelles Passwort")},
			{QStringLiteral("New password"), QStringLiteral("Neues Passwort")},
			{QStringLiteral("Required to change password"), QStringLiteral("Zum Ändern des Passworts erforderlich")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Leer lassen, um das aktuelle Passwort beizubehalten")},
			{QStringLiteral("Save profile"), QStringLiteral("Profil speichern")},
			{QStringLiteral("Language"), QStringLiteral("Sprache")},
			{QStringLiteral("Profile saved."), QStringLiteral("Profil gespeichert.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Profil wird gespeichert…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Gib zum Ändern des Passworts das aktuelle und das neue Passwort ein.")}}},
		{QStringLiteral("pt"), {
			{QStringLiteral("Settings"), QStringLiteral("Configurações")},
			{QStringLiteral("Profile settings"), QStringLiteral("Configurações do perfil")},
			{QStringLiteral("App settings"), QStringLiteral("Configurações do aplicativo")},
			{QStringLiteral("Nickname"), QStringLiteral("Apelido")},
			{QStringLiteral("Country / flag"), QStringLiteral("País / bandeira")},
			{QStringLiteral("Current password"), QStringLiteral("Senha atual")},
			{QStringLiteral("New password"), QStringLiteral("Nova senha")},
			{QStringLiteral("Required to change password"), QStringLiteral("Obrigatória para alterar a senha")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Deixe em branco para manter a senha atual")},
			{QStringLiteral("Save profile"), QStringLiteral("Salvar perfil")},
			{QStringLiteral("Language"), QStringLiteral("Idioma")},
			{QStringLiteral("Profile saved."), QStringLiteral("Perfil salvo.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Salvando perfil…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Informe as senhas atual e nova para alterá-la.")}}},
		{QStringLiteral("it"), {
			{QStringLiteral("Settings"), QStringLiteral("Impostazioni")},
			{QStringLiteral("Profile settings"), QStringLiteral("Impostazioni profilo")},
			{QStringLiteral("App settings"), QStringLiteral("Impostazioni app")},
			{QStringLiteral("Nickname"), QStringLiteral("Nickname")},
			{QStringLiteral("Country / flag"), QStringLiteral("Paese / bandiera")},
			{QStringLiteral("Current password"), QStringLiteral("Password attuale")},
			{QStringLiteral("New password"), QStringLiteral("Nuova password")},
			{QStringLiteral("Required to change password"), QStringLiteral("Obbligatoria per cambiare la password")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Lascia vuoto per mantenere la password attuale")},
			{QStringLiteral("Save profile"), QStringLiteral("Salva profilo")},
			{QStringLiteral("Language"), QStringLiteral("Lingua")},
			{QStringLiteral("Profile saved."), QStringLiteral("Profilo salvato.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Salvataggio del profilo…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Inserisci la password attuale e quella nuova per cambiarla.")}}},
		{QStringLiteral("ru"), {
			{QStringLiteral("Settings"), QStringLiteral("Настройки")},
			{QStringLiteral("Profile settings"), QStringLiteral("Настройки профиля")},
			{QStringLiteral("App settings"), QStringLiteral("Настройки приложения")},
			{QStringLiteral("Nickname"), QStringLiteral("Имя пользователя")},
			{QStringLiteral("Country / flag"), QStringLiteral("Страна / флаг")},
			{QStringLiteral("Current password"), QStringLiteral("Текущий пароль")},
			{QStringLiteral("New password"), QStringLiteral("Новый пароль")},
			{QStringLiteral("Required to change password"), QStringLiteral("Требуется для смены пароля")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("Оставьте пустым, чтобы сохранить текущий пароль")},
			{QStringLiteral("Save profile"), QStringLiteral("Сохранить профиль")},
			{QStringLiteral("Language"), QStringLiteral("Язык")},
			{QStringLiteral("Profile saved."), QStringLiteral("Профиль сохранён.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("Сохранение профиля…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("Для смены пароля введите текущий и новый пароль.")}}},
		{QStringLiteral("zh"), {
			{QStringLiteral("Settings"), QStringLiteral("设置")},
			{QStringLiteral("Profile settings"), QStringLiteral("个人资料设置")},
			{QStringLiteral("App settings"), QStringLiteral("应用设置")},
			{QStringLiteral("Nickname"), QStringLiteral("昵称")},
			{QStringLiteral("Country / flag"), QStringLiteral("国家 / 国旗")},
			{QStringLiteral("Current password"), QStringLiteral("当前密码")},
			{QStringLiteral("New password"), QStringLiteral("新密码")},
			{QStringLiteral("Required to change password"), QStringLiteral("更改密码时必填")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("留空以保留当前密码")},
			{QStringLiteral("Save profile"), QStringLiteral("保存个人资料")},
			{QStringLiteral("Language"), QStringLiteral("语言")},
			{QStringLiteral("Profile saved."), QStringLiteral("个人资料已保存。")},
			{QStringLiteral("Saving profile…"), QStringLiteral("正在保存个人资料…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("请输入当前密码和新密码以更改密码。")}}},
		{QStringLiteral("ja"), {
			{QStringLiteral("Settings"), QStringLiteral("設定")},
			{QStringLiteral("Profile settings"), QStringLiteral("プロフィール設定")},
			{QStringLiteral("App settings"), QStringLiteral("アプリ設定")},
			{QStringLiteral("Nickname"), QStringLiteral("ニックネーム")},
			{QStringLiteral("Country / flag"), QStringLiteral("国 / 国旗")},
			{QStringLiteral("Current password"), QStringLiteral("現在のパスワード")},
			{QStringLiteral("New password"), QStringLiteral("新しいパスワード")},
			{QStringLiteral("Required to change password"), QStringLiteral("変更には必須です")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("現在のパスワードを維持する場合は空欄")},
			{QStringLiteral("Save profile"), QStringLiteral("プロフィールを保存")},
			{QStringLiteral("Language"), QStringLiteral("言語")},
			{QStringLiteral("Profile saved."), QStringLiteral("プロフィールを保存しました。")},
			{QStringLiteral("Saving profile…"), QStringLiteral("プロフィールを保存中…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("パスワードを変更するには現在と新しいパスワードを入力してください。")}}},
		{QStringLiteral("ko"), {
			{QStringLiteral("Settings"), QStringLiteral("설정")},
			{QStringLiteral("Profile settings"), QStringLiteral("프로필 설정")},
			{QStringLiteral("App settings"), QStringLiteral("앱 설정")},
			{QStringLiteral("Nickname"), QStringLiteral("닉네임")},
			{QStringLiteral("Country / flag"), QStringLiteral("국가 / 국기")},
			{QStringLiteral("Current password"), QStringLiteral("현재 비밀번호")},
			{QStringLiteral("New password"), QStringLiteral("새 비밀번호")},
			{QStringLiteral("Required to change password"), QStringLiteral("비밀번호 변경 시 필요")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("현재 비밀번호를 유지하려면 비워 두세요")},
			{QStringLiteral("Save profile"), QStringLiteral("프로필 저장")},
			{QStringLiteral("Language"), QStringLiteral("언어")},
			{QStringLiteral("Profile saved."), QStringLiteral("프로필을 저장했습니다.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("프로필 저장 중…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("비밀번호를 변경하려면 현재 비밀번호와 새 비밀번호를 모두 입력하세요.")}}},
		{QStringLiteral("ar"), {
			{QStringLiteral("Settings"), QStringLiteral("الإعدادات")},
			{QStringLiteral("Profile settings"), QStringLiteral("إعدادات الملف الشخصي")},
			{QStringLiteral("App settings"), QStringLiteral("إعدادات التطبيق")},
			{QStringLiteral("Nickname"), QStringLiteral("الاسم المستعار")},
			{QStringLiteral("Country / flag"), QStringLiteral("الدولة / العلم")},
			{QStringLiteral("Current password"), QStringLiteral("كلمة المرور الحالية")},
			{QStringLiteral("New password"), QStringLiteral("كلمة المرور الجديدة")},
			{QStringLiteral("Required to change password"), QStringLiteral("مطلوب لتغيير كلمة المرور")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("اتركه فارغًا للاحتفاظ بكلمة المرور الحالية")},
			{QStringLiteral("Save profile"), QStringLiteral("حفظ الملف الشخصي")},
			{QStringLiteral("Language"), QStringLiteral("اللغة")},
			{QStringLiteral("Profile saved."), QStringLiteral("تم حفظ الملف الشخصي.")},
			{QStringLiteral("Saving profile…"), QStringLiteral("جارٍ حفظ الملف الشخصي…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("أدخل كلمة المرور الحالية والجديدة لتغيير كلمة المرور.")}}},
		{QStringLiteral("hi"), {
			{QStringLiteral("Settings"), QStringLiteral("सेटिंग्स")},
			{QStringLiteral("Profile settings"), QStringLiteral("प्रोफ़ाइल सेटिंग्स")},
			{QStringLiteral("App settings"), QStringLiteral("ऐप सेटिंग्स")},
			{QStringLiteral("Nickname"), QStringLiteral("उपनाम")},
			{QStringLiteral("Country / flag"), QStringLiteral("देश / ध्वज")},
			{QStringLiteral("Current password"), QStringLiteral("वर्तमान पासवर्ड")},
			{QStringLiteral("New password"), QStringLiteral("नया पासवर्ड")},
			{QStringLiteral("Required to change password"), QStringLiteral("पासवर्ड बदलने के लिए आवश्यक")},
			{QStringLiteral("Leave blank to keep current password"), QStringLiteral("वर्तमान पासवर्ड रखने के लिए खाली छोड़ें")},
			{QStringLiteral("Save profile"), QStringLiteral("प्रोफ़ाइल सहेजें")},
			{QStringLiteral("Language"), QStringLiteral("भाषा")},
			{QStringLiteral("Profile saved."), QStringLiteral("प्रोफ़ाइल सहेजी गई।")},
			{QStringLiteral("Saving profile…"), QStringLiteral("प्रोफ़ाइल सहेजी जा रही है…")},
			{QStringLiteral("Enter both current and new passwords to change your password."), QStringLiteral("पासवर्ड बदलने के लिए वर्तमान और नया पासवर्ड दर्ज करें।")}}}
	};
	static const QHash<QString, QHash<QString, QString>> commonTranslations{
		{QStringLiteral("es"), {
			{QStringLiteral("Username"), QStringLiteral("Usuario")}, {QStringLiteral("Password"), QStringLiteral("Contraseña")},
			{QStringLiteral("Display name"), QStringLiteral("Nombre visible")}, {QStringLiteral("Create a new account"), QStringLiteral("Crear una cuenta nueva")},
			{QStringLiteral("Continue"), QStringLiteral("Continuar")}, {QStringLiteral("Lobby"), QStringLiteral("Sala")},
			{QStringLiteral("Rooms"), QStringLiteral("Salas")}, {QStringLiteral("Leaderboards"), QStringLiteral("Clasificaciones")},
			{QStringLiteral("Refresh"), QStringLiteral("Actualizar")}, {QStringLiteral("Join selected room"), QStringLiteral("Unirse a la sala seleccionada")},
			{QStringLiteral("Sign out"), QStringLiteral("Cerrar sesión")}, {QStringLiteral("Join as player"), QStringLiteral("Unirse como jugador")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Unirse como espectador")}, {QStringLiteral("Room"), QStringLiteral("Sala")},
			{QStringLiteral("Players"), QStringLiteral("Jugadores")}, {QStringLiteral("Round timer"), QStringLiteral("Tiempo de ronda")},
			{QStringLiteral("Ping"), QStringLiteral("Latencia")}, {QStringLiteral("Server"), QStringLiteral("Servidor")},
			{QStringLiteral("Rank"), QStringLiteral("Rango")}, {QStringLiteral("Player"), QStringLiteral("Jugador")},
			{QStringLiteral("Country"), QStringLiteral("País")}, {QStringLiteral("Guessing points"), QStringLiteral("Puntos por adivinar")},
			{QStringLiteral("Drawing points"), QStringLiteral("Puntos por dibujar")}, {QStringLiteral("Guessing"), QStringLiteral("Adivinar")},
			{QStringLiteral("Drawing"), QStringLiteral("Dibujo")}}},
		{QStringLiteral("fr"), {
			{QStringLiteral("Username"), QStringLiteral("Nom d’utilisateur")}, {QStringLiteral("Password"), QStringLiteral("Mot de passe")},
			{QStringLiteral("Display name"), QStringLiteral("Nom affiché")}, {QStringLiteral("Create a new account"), QStringLiteral("Créer un compte")},
			{QStringLiteral("Continue"), QStringLiteral("Continuer")}, {QStringLiteral("Lobby"), QStringLiteral("Salon")},
			{QStringLiteral("Rooms"), QStringLiteral("Salons")}, {QStringLiteral("Leaderboards"), QStringLiteral("Classements")},
			{QStringLiteral("Refresh"), QStringLiteral("Actualiser")}, {QStringLiteral("Join selected room"), QStringLiteral("Rejoindre le salon sélectionné")},
			{QStringLiteral("Sign out"), QStringLiteral("Se déconnecter")}, {QStringLiteral("Join as player"), QStringLiteral("Rejoindre comme joueur")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Rejoindre comme spectateur")}, {QStringLiteral("Room"), QStringLiteral("Salon")},
			{QStringLiteral("Players"), QStringLiteral("Joueurs")}, {QStringLiteral("Round timer"), QStringLiteral("Durée du tour")},
			{QStringLiteral("Ping"), QStringLiteral("Latence")}, {QStringLiteral("Server"), QStringLiteral("Serveur")},
			{QStringLiteral("Rank"), QStringLiteral("Rang")}, {QStringLiteral("Player"), QStringLiteral("Joueur")},
			{QStringLiteral("Country"), QStringLiteral("Pays")}, {QStringLiteral("Guessing points"), QStringLiteral("Points de devinette")},
			{QStringLiteral("Drawing points"), QStringLiteral("Points de dessin")}, {QStringLiteral("Guessing"), QStringLiteral("Devinettes")},
			{QStringLiteral("Drawing"), QStringLiteral("Dessin")}}},
		{QStringLiteral("de"), {
			{QStringLiteral("Username"), QStringLiteral("Benutzername")}, {QStringLiteral("Password"), QStringLiteral("Passwort")},
			{QStringLiteral("Display name"), QStringLiteral("Anzeigename")}, {QStringLiteral("Create a new account"), QStringLiteral("Neues Konto erstellen")},
			{QStringLiteral("Continue"), QStringLiteral("Weiter")}, {QStringLiteral("Lobby"), QStringLiteral("Lobby")},
			{QStringLiteral("Rooms"), QStringLiteral("Räume")}, {QStringLiteral("Leaderboards"), QStringLiteral("Bestenlisten")},
			{QStringLiteral("Refresh"), QStringLiteral("Aktualisieren")}, {QStringLiteral("Join selected room"), QStringLiteral("Ausgewähltem Raum beitreten")},
			{QStringLiteral("Sign out"), QStringLiteral("Abmelden")}, {QStringLiteral("Join as player"), QStringLiteral("Als Spieler beitreten")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Als Zuschauer beitreten")}, {QStringLiteral("Room"), QStringLiteral("Raum")},
			{QStringLiteral("Players"), QStringLiteral("Spieler")}, {QStringLiteral("Round timer"), QStringLiteral("Rundenzeit")},
			{QStringLiteral("Ping"), QStringLiteral("Latenz")}, {QStringLiteral("Server"), QStringLiteral("Server")},
			{QStringLiteral("Rank"), QStringLiteral("Rang")}, {QStringLiteral("Player"), QStringLiteral("Spieler")},
			{QStringLiteral("Country"), QStringLiteral("Land")}, {QStringLiteral("Guessing points"), QStringLiteral("Ratepunkte")},
			{QStringLiteral("Drawing points"), QStringLiteral("Zeichenpunkte")}, {QStringLiteral("Guessing"), QStringLiteral("Raten")},
			{QStringLiteral("Drawing"), QStringLiteral("Zeichnen")}}},
		{QStringLiteral("pt"), {
			{QStringLiteral("Username"), QStringLiteral("Nome de usuário")}, {QStringLiteral("Password"), QStringLiteral("Senha")},
			{QStringLiteral("Display name"), QStringLiteral("Nome de exibição")}, {QStringLiteral("Create a new account"), QStringLiteral("Criar uma nova conta")},
			{QStringLiteral("Continue"), QStringLiteral("Continuar")}, {QStringLiteral("Lobby"), QStringLiteral("Sala de espera")},
			{QStringLiteral("Rooms"), QStringLiteral("Salas")}, {QStringLiteral("Leaderboards"), QStringLiteral("Classificações")},
			{QStringLiteral("Refresh"), QStringLiteral("Atualizar")}, {QStringLiteral("Join selected room"), QStringLiteral("Entrar na sala selecionada")},
			{QStringLiteral("Sign out"), QStringLiteral("Sair")}, {QStringLiteral("Join as player"), QStringLiteral("Entrar como jogador")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Entrar como espectador")}, {QStringLiteral("Room"), QStringLiteral("Sala")},
			{QStringLiteral("Players"), QStringLiteral("Jogadores")}, {QStringLiteral("Round timer"), QStringLiteral("Tempo da rodada")},
			{QStringLiteral("Ping"), QStringLiteral("Latência")}, {QStringLiteral("Server"), QStringLiteral("Servidor")},
			{QStringLiteral("Rank"), QStringLiteral("Classificação")}, {QStringLiteral("Player"), QStringLiteral("Jogador")},
			{QStringLiteral("Country"), QStringLiteral("País")}, {QStringLiteral("Guessing points"), QStringLiteral("Pontos de adivinhação")},
			{QStringLiteral("Drawing points"), QStringLiteral("Pontos de desenho")}, {QStringLiteral("Guessing"), QStringLiteral("Adivinhação")},
			{QStringLiteral("Drawing"), QStringLiteral("Desenho")}}},
		{QStringLiteral("it"), {
			{QStringLiteral("Username"), QStringLiteral("Nome utente")}, {QStringLiteral("Password"), QStringLiteral("Password")},
			{QStringLiteral("Display name"), QStringLiteral("Nome visualizzato")}, {QStringLiteral("Create a new account"), QStringLiteral("Crea un nuovo account")},
			{QStringLiteral("Continue"), QStringLiteral("Continua")}, {QStringLiteral("Lobby"), QStringLiteral("Lobby")},
			{QStringLiteral("Rooms"), QStringLiteral("Stanze")}, {QStringLiteral("Leaderboards"), QStringLiteral("Classifiche")},
			{QStringLiteral("Refresh"), QStringLiteral("Aggiorna")}, {QStringLiteral("Join selected room"), QStringLiteral("Entra nella stanza selezionata")},
			{QStringLiteral("Sign out"), QStringLiteral("Esci")}, {QStringLiteral("Join as player"), QStringLiteral("Entra come giocatore")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Entra come spettatore")}, {QStringLiteral("Room"), QStringLiteral("Stanza")},
			{QStringLiteral("Players"), QStringLiteral("Giocatori")}, {QStringLiteral("Round timer"), QStringLiteral("Tempo del round")},
			{QStringLiteral("Ping"), QStringLiteral("Latenza")}, {QStringLiteral("Server"), QStringLiteral("Server")},
			{QStringLiteral("Rank"), QStringLiteral("Posizione")}, {QStringLiteral("Player"), QStringLiteral("Giocatore")},
			{QStringLiteral("Country"), QStringLiteral("Paese")}, {QStringLiteral("Guessing points"), QStringLiteral("Punti indovinati")},
			{QStringLiteral("Drawing points"), QStringLiteral("Punti disegno")}, {QStringLiteral("Guessing"), QStringLiteral("Indovinare")},
			{QStringLiteral("Drawing"), QStringLiteral("Disegno")}}},
		{QStringLiteral("ru"), {
			{QStringLiteral("Username"), QStringLiteral("Имя пользователя")}, {QStringLiteral("Password"), QStringLiteral("Пароль")},
			{QStringLiteral("Display name"), QStringLiteral("Отображаемое имя")}, {QStringLiteral("Create a new account"), QStringLiteral("Создать аккаунт")},
			{QStringLiteral("Continue"), QStringLiteral("Продолжить")}, {QStringLiteral("Lobby"), QStringLiteral("Лобби")},
			{QStringLiteral("Rooms"), QStringLiteral("Комнаты")}, {QStringLiteral("Leaderboards"), QStringLiteral("Таблицы лидеров")},
			{QStringLiteral("Refresh"), QStringLiteral("Обновить")}, {QStringLiteral("Join selected room"), QStringLiteral("Войти в выбранную комнату")},
			{QStringLiteral("Sign out"), QStringLiteral("Выйти")}, {QStringLiteral("Join as player"), QStringLiteral("Войти как игрок")},
			{QStringLiteral("Join as spectator"), QStringLiteral("Войти как зритель")}, {QStringLiteral("Room"), QStringLiteral("Комната")},
			{QStringLiteral("Players"), QStringLiteral("Игроки")}, {QStringLiteral("Round timer"), QStringLiteral("Время раунда")},
			{QStringLiteral("Ping"), QStringLiteral("Задержка")}, {QStringLiteral("Server"), QStringLiteral("Сервер")},
			{QStringLiteral("Rank"), QStringLiteral("Ранг")}, {QStringLiteral("Player"), QStringLiteral("Игрок")},
			{QStringLiteral("Country"), QStringLiteral("Страна")}, {QStringLiteral("Guessing points"), QStringLiteral("Очки за угадывание")},
			{QStringLiteral("Drawing points"), QStringLiteral("Очки за рисование")}, {QStringLiteral("Guessing"), QStringLiteral("Угадывание")},
			{QStringLiteral("Drawing"), QStringLiteral("Рисование")}}},
		{QStringLiteral("zh"), {
			{QStringLiteral("Username"), QStringLiteral("用户名")}, {QStringLiteral("Password"), QStringLiteral("密码")},
			{QStringLiteral("Display name"), QStringLiteral("显示名称")}, {QStringLiteral("Create a new account"), QStringLiteral("创建新账户")},
			{QStringLiteral("Continue"), QStringLiteral("继续")}, {QStringLiteral("Lobby"), QStringLiteral("大厅")},
			{QStringLiteral("Rooms"), QStringLiteral("房间")}, {QStringLiteral("Leaderboards"), QStringLiteral("排行榜")},
			{QStringLiteral("Refresh"), QStringLiteral("刷新")}, {QStringLiteral("Join selected room"), QStringLiteral("加入所选房间")},
			{QStringLiteral("Sign out"), QStringLiteral("退出登录")}, {QStringLiteral("Join as player"), QStringLiteral("以玩家身份加入")},
			{QStringLiteral("Join as spectator"), QStringLiteral("以观众身份加入")}, {QStringLiteral("Room"), QStringLiteral("房间")},
			{QStringLiteral("Players"), QStringLiteral("玩家")}, {QStringLiteral("Round timer"), QStringLiteral("回合计时")},
			{QStringLiteral("Ping"), QStringLiteral("延迟")}, {QStringLiteral("Server"), QStringLiteral("服务器")},
			{QStringLiteral("Rank"), QStringLiteral("排名")}, {QStringLiteral("Player"), QStringLiteral("玩家")},
			{QStringLiteral("Country"), QStringLiteral("国家")}, {QStringLiteral("Guessing points"), QStringLiteral("猜词得分")},
			{QStringLiteral("Drawing points"), QStringLiteral("绘画得分")}, {QStringLiteral("Guessing"), QStringLiteral("猜词")},
			{QStringLiteral("Drawing"), QStringLiteral("绘画")}}},
		{QStringLiteral("ja"), {
			{QStringLiteral("Username"), QStringLiteral("ユーザー名")}, {QStringLiteral("Password"), QStringLiteral("パスワード")},
			{QStringLiteral("Display name"), QStringLiteral("表示名")}, {QStringLiteral("Create a new account"), QStringLiteral("新しいアカウントを作成")},
			{QStringLiteral("Continue"), QStringLiteral("続行")}, {QStringLiteral("Lobby"), QStringLiteral("ロビー")},
			{QStringLiteral("Rooms"), QStringLiteral("ルーム")}, {QStringLiteral("Leaderboards"), QStringLiteral("ランキング")},
			{QStringLiteral("Refresh"), QStringLiteral("更新")}, {QStringLiteral("Join selected room"), QStringLiteral("選択したルームに参加")},
			{QStringLiteral("Sign out"), QStringLiteral("ログアウト")}, {QStringLiteral("Join as player"), QStringLiteral("プレイヤーとして参加")},
			{QStringLiteral("Join as spectator"), QStringLiteral("観戦者として参加")}, {QStringLiteral("Room"), QStringLiteral("ルーム")},
			{QStringLiteral("Players"), QStringLiteral("プレイヤー")}, {QStringLiteral("Round timer"), QStringLiteral("ラウンド時間")},
			{QStringLiteral("Ping"), QStringLiteral("遅延")}, {QStringLiteral("Server"), QStringLiteral("サーバー")},
			{QStringLiteral("Rank"), QStringLiteral("順位")}, {QStringLiteral("Player"), QStringLiteral("プレイヤー")},
			{QStringLiteral("Country"), QStringLiteral("国")}, {QStringLiteral("Guessing points"), QStringLiteral("推測ポイント")},
			{QStringLiteral("Drawing points"), QStringLiteral("描画ポイント")}, {QStringLiteral("Guessing"), QStringLiteral("推測")},
			{QStringLiteral("Drawing"), QStringLiteral("描画")}}},
		{QStringLiteral("ko"), {
			{QStringLiteral("Username"), QStringLiteral("사용자 이름")}, {QStringLiteral("Password"), QStringLiteral("비밀번호")},
			{QStringLiteral("Display name"), QStringLiteral("표시 이름")}, {QStringLiteral("Create a new account"), QStringLiteral("새 계정 만들기")},
			{QStringLiteral("Continue"), QStringLiteral("계속")}, {QStringLiteral("Lobby"), QStringLiteral("대기실")},
			{QStringLiteral("Rooms"), QStringLiteral("방")}, {QStringLiteral("Leaderboards"), QStringLiteral("순위표")},
			{QStringLiteral("Refresh"), QStringLiteral("새로 고침")}, {QStringLiteral("Join selected room"), QStringLiteral("선택한 방 참가")},
			{QStringLiteral("Sign out"), QStringLiteral("로그아웃")}, {QStringLiteral("Join as player"), QStringLiteral("플레이어로 참가")},
			{QStringLiteral("Join as spectator"), QStringLiteral("관전자로 참가")}, {QStringLiteral("Room"), QStringLiteral("방")},
			{QStringLiteral("Players"), QStringLiteral("플레이어")}, {QStringLiteral("Round timer"), QStringLiteral("라운드 시간")},
			{QStringLiteral("Ping"), QStringLiteral("지연 시간")}, {QStringLiteral("Server"), QStringLiteral("서버")},
			{QStringLiteral("Rank"), QStringLiteral("순위")}, {QStringLiteral("Player"), QStringLiteral("플레이어")},
			{QStringLiteral("Country"), QStringLiteral("국가")}, {QStringLiteral("Guessing points"), QStringLiteral("추측 점수")},
			{QStringLiteral("Drawing points"), QStringLiteral("그림 점수")}, {QStringLiteral("Guessing"), QStringLiteral("추측")},
			{QStringLiteral("Drawing"), QStringLiteral("그리기")}}},
		{QStringLiteral("ar"), {
			{QStringLiteral("Username"), QStringLiteral("اسم المستخدم")}, {QStringLiteral("Password"), QStringLiteral("كلمة المرور")},
			{QStringLiteral("Display name"), QStringLiteral("الاسم المعروض")}, {QStringLiteral("Create a new account"), QStringLiteral("إنشاء حساب جديد")},
			{QStringLiteral("Continue"), QStringLiteral("متابعة")}, {QStringLiteral("Lobby"), QStringLiteral("الردهة")},
			{QStringLiteral("Rooms"), QStringLiteral("الغرف")}, {QStringLiteral("Leaderboards"), QStringLiteral("لوحات الصدارة")},
			{QStringLiteral("Refresh"), QStringLiteral("تحديث")}, {QStringLiteral("Join selected room"), QStringLiteral("انضم إلى الغرفة المحددة")},
			{QStringLiteral("Sign out"), QStringLiteral("تسجيل الخروج")}, {QStringLiteral("Join as player"), QStringLiteral("انضم كلاعب")},
			{QStringLiteral("Join as spectator"), QStringLiteral("انضم كمشاهد")}, {QStringLiteral("Room"), QStringLiteral("الغرفة")},
			{QStringLiteral("Players"), QStringLiteral("اللاعبون")}, {QStringLiteral("Round timer"), QStringLiteral("مؤقت الجولة")},
			{QStringLiteral("Ping"), QStringLiteral("زمن الاستجابة")}, {QStringLiteral("Server"), QStringLiteral("الخادم")},
			{QStringLiteral("Rank"), QStringLiteral("الترتيب")}, {QStringLiteral("Player"), QStringLiteral("اللاعب")},
			{QStringLiteral("Country"), QStringLiteral("الدولة")}, {QStringLiteral("Guessing points"), QStringLiteral("نقاط التخمين")},
			{QStringLiteral("Drawing points"), QStringLiteral("نقاط الرسم")}, {QStringLiteral("Guessing"), QStringLiteral("التخمين")},
			{QStringLiteral("Drawing"), QStringLiteral("الرسم")}}},
		{QStringLiteral("hi"), {
			{QStringLiteral("Username"), QStringLiteral("उपयोगकर्ता नाम")}, {QStringLiteral("Password"), QStringLiteral("पासवर्ड")},
			{QStringLiteral("Display name"), QStringLiteral("प्रदर्शित नाम")}, {QStringLiteral("Create a new account"), QStringLiteral("नया खाता बनाएँ")},
			{QStringLiteral("Continue"), QStringLiteral("जारी रखें")}, {QStringLiteral("Lobby"), QStringLiteral("लॉबी")},
			{QStringLiteral("Rooms"), QStringLiteral("कमरे")}, {QStringLiteral("Leaderboards"), QStringLiteral("लीडरबोर्ड")},
			{QStringLiteral("Refresh"), QStringLiteral("रीफ़्रेश करें")}, {QStringLiteral("Join selected room"), QStringLiteral("चुने हुए कमरे में शामिल हों")},
			{QStringLiteral("Sign out"), QStringLiteral("साइन आउट")}, {QStringLiteral("Join as player"), QStringLiteral("खिलाड़ी के रूप में जुड़ें")},
			{QStringLiteral("Join as spectator"), QStringLiteral("दर्शक के रूप में जुड़ें")}, {QStringLiteral("Room"), QStringLiteral("कमरा")},
			{QStringLiteral("Players"), QStringLiteral("खिलाड़ी")}, {QStringLiteral("Round timer"), QStringLiteral("राउंड समय")},
			{QStringLiteral("Ping"), QStringLiteral("विलंब")}, {QStringLiteral("Server"), QStringLiteral("सर्वर")},
			{QStringLiteral("Rank"), QStringLiteral("रैंक")}, {QStringLiteral("Player"), QStringLiteral("खिलाड़ी")},
			{QStringLiteral("Country"), QStringLiteral("देश")}, {QStringLiteral("Guessing points"), QStringLiteral("अनुमान अंक")},
			{QStringLiteral("Drawing points"), QStringLiteral("चित्रकारी अंक")}, {QStringLiteral("Guessing"), QStringLiteral("अनुमान")},
			{QStringLiteral("Drawing"), QStringLiteral("चित्रकारी")}}}
	};
	const QString commonText = commonTranslations.value(languageCode).value(source);
	if (!commonText.isEmpty())
		return commonText;
	const QString settingsText = translations.value(languageCode).value(source);
	if (!settingsText.isEmpty())
		return settingsText;
	static const QStringList controlSources{
		QStringLiteral("Color"), QStringLiteral("Text size"), QStringLiteral("Fill shapes"),
		QStringLiteral("Add"), QStringLiteral("Delete"), QStringLiteral("Rename"),
		QStringLiteral("Hide"), QStringLiteral("Lock"), QStringLiteral("Move up"),
		QStringLiteral("Move down"), QStringLiteral("Guess"), QStringLiteral("Send"),
		QStringLiteral("Enable microphone"), QStringLiteral("Microphone off"),
		QStringLiteral("Type your guess"), QStringLiteral("Send a room message"),
		QStringLiteral("Leave room"), QStringLiteral("Waiting for players")};
	static const QHash<QString, QStringList> controlTranslations{
		{QStringLiteral("es"), {QStringLiteral("Color"), QStringLiteral("Tamaño del texto"), QStringLiteral("Rellenar formas"), QStringLiteral("Añadir"), QStringLiteral("Eliminar"), QStringLiteral("Renombrar"), QStringLiteral("Ocultar"), QStringLiteral("Bloquear"), QStringLiteral("Mover arriba"), QStringLiteral("Mover abajo"), QStringLiteral("Adivinar"), QStringLiteral("Enviar"), QStringLiteral("Activar micrófono"), QStringLiteral("Micrófono apagado"), QStringLiteral("Escribe tu respuesta"), QStringLiteral("Enviar mensaje a la sala"), QStringLiteral("Salir de la sala"), QStringLiteral("Esperando jugadores")}},
		{QStringLiteral("fr"), {QStringLiteral("Couleur"), QStringLiteral("Taille du texte"), QStringLiteral("Remplir les formes"), QStringLiteral("Ajouter"), QStringLiteral("Supprimer"), QStringLiteral("Renommer"), QStringLiteral("Masquer"), QStringLiteral("Verrouiller"), QStringLiteral("Monter"), QStringLiteral("Descendre"), QStringLiteral("Deviner"), QStringLiteral("Envoyer"), QStringLiteral("Activer le micro"), QStringLiteral("Micro désactivé"), QStringLiteral("Saisissez votre réponse"), QStringLiteral("Envoyer un message au salon"), QStringLiteral("Quitter le salon"), QStringLiteral("En attente des joueurs")}},
		{QStringLiteral("de"), {QStringLiteral("Farbe"), QStringLiteral("Textgröße"), QStringLiteral("Formen füllen"), QStringLiteral("Hinzufügen"), QStringLiteral("Löschen"), QStringLiteral("Umbenennen"), QStringLiteral("Ausblenden"), QStringLiteral("Sperren"), QStringLiteral("Nach oben"), QStringLiteral("Nach unten"), QStringLiteral("Raten"), QStringLiteral("Senden"), QStringLiteral("Mikrofon aktivieren"), QStringLiteral("Mikrofon aus"), QStringLiteral("Deine Antwort eingeben"), QStringLiteral("Raumnachricht senden"), QStringLiteral("Raum verlassen"), QStringLiteral("Warte auf Spieler")}},
		{QStringLiteral("pt"), {QStringLiteral("Cor"), QStringLiteral("Tamanho do texto"), QStringLiteral("Preencher formas"), QStringLiteral("Adicionar"), QStringLiteral("Excluir"), QStringLiteral("Renomear"), QStringLiteral("Ocultar"), QStringLiteral("Bloquear"), QStringLiteral("Mover para cima"), QStringLiteral("Mover para baixo"), QStringLiteral("Adivinhar"), QStringLiteral("Enviar"), QStringLiteral("Ativar microfone"), QStringLiteral("Microfone desligado"), QStringLiteral("Digite seu palpite"), QStringLiteral("Enviar mensagem na sala"), QStringLiteral("Sair da sala"), QStringLiteral("Aguardando jogadores")}},
		{QStringLiteral("it"), {QStringLiteral("Colore"), QStringLiteral("Dimensione testo"), QStringLiteral("Riempi forme"), QStringLiteral("Aggiungi"), QStringLiteral("Elimina"), QStringLiteral("Rinomina"), QStringLiteral("Nascondi"), QStringLiteral("Blocca"), QStringLiteral("Sposta su"), QStringLiteral("Sposta giù"), QStringLiteral("Indovina"), QStringLiteral("Invia"), QStringLiteral("Attiva microfono"), QStringLiteral("Microfono spento"), QStringLiteral("Scrivi la tua risposta"), QStringLiteral("Invia un messaggio alla stanza"), QStringLiteral("Esci dalla stanza"), QStringLiteral("In attesa dei giocatori")}},
		{QStringLiteral("ru"), {QStringLiteral("Цвет"), QStringLiteral("Размер текста"), QStringLiteral("Заливка фигур"), QStringLiteral("Добавить"), QStringLiteral("Удалить"), QStringLiteral("Переименовать"), QStringLiteral("Скрыть"), QStringLiteral("Заблокировать"), QStringLiteral("Вверх"), QStringLiteral("Вниз"), QStringLiteral("Угадать"), QStringLiteral("Отправить"), QStringLiteral("Включить микрофон"), QStringLiteral("Микрофон выключен"), QStringLiteral("Введите ответ"), QStringLiteral("Отправить сообщение в комнату"), QStringLiteral("Покинуть комнату"), QStringLiteral("Ожидание игроков")}},
		{QStringLiteral("zh"), {QStringLiteral("颜色"), QStringLiteral("文字大小"), QStringLiteral("填充形状"), QStringLiteral("添加"), QStringLiteral("删除"), QStringLiteral("重命名"), QStringLiteral("隐藏"), QStringLiteral("锁定"), QStringLiteral("上移"), QStringLiteral("下移"), QStringLiteral("猜词"), QStringLiteral("发送"), QStringLiteral("启用麦克风"), QStringLiteral("麦克风已关闭"), QStringLiteral("输入你的答案"), QStringLiteral("发送房间消息"), QStringLiteral("离开房间"), QStringLiteral("等待玩家")}},
		{QStringLiteral("ja"), {QStringLiteral("色"), QStringLiteral("文字サイズ"), QStringLiteral("図形を塗りつぶす"), QStringLiteral("追加"), QStringLiteral("削除"), QStringLiteral("名前を変更"), QStringLiteral("非表示"), QStringLiteral("ロック"), QStringLiteral("上へ移動"), QStringLiteral("下へ移動"), QStringLiteral("推測"), QStringLiteral("送信"), QStringLiteral("マイクを有効化"), QStringLiteral("マイクオフ"), QStringLiteral("答えを入力"), QStringLiteral("ルームにメッセージを送信"), QStringLiteral("ルームを退出"), QStringLiteral("プレイヤーを待っています")}},
		{QStringLiteral("ko"), {QStringLiteral("색상"), QStringLiteral("텍스트 크기"), QStringLiteral("도형 채우기"), QStringLiteral("추가"), QStringLiteral("삭제"), QStringLiteral("이름 변경"), QStringLiteral("숨기기"), QStringLiteral("잠금"), QStringLiteral("위로 이동"), QStringLiteral("아래로 이동"), QStringLiteral("추측"), QStringLiteral("보내기"), QStringLiteral("마이크 켜기"), QStringLiteral("마이크 꺼짐"), QStringLiteral("정답 입력"), QStringLiteral("방 메시지 보내기"), QStringLiteral("방 나가기"), QStringLiteral("플레이어 대기 중")}},
		{QStringLiteral("ar"), {QStringLiteral("اللون"), QStringLiteral("حجم النص"), QStringLiteral("تعبئة الأشكال"), QStringLiteral("إضافة"), QStringLiteral("حذف"), QStringLiteral("إعادة التسمية"), QStringLiteral("إخفاء"), QStringLiteral("قفل"), QStringLiteral("تحريك لأعلى"), QStringLiteral("تحريك لأسفل"), QStringLiteral("خمن"), QStringLiteral("إرسال"), QStringLiteral("تشغيل الميكروفون"), QStringLiteral("الميكروفون متوقف"), QStringLiteral("اكتب تخمينك"), QStringLiteral("إرسال رسالة للغرفة"), QStringLiteral("مغادرة الغرفة"), QStringLiteral("بانتظار اللاعبين")}},
		{QStringLiteral("hi"), {QStringLiteral("रंग"), QStringLiteral("पाठ का आकार"), QStringLiteral("आकार भरें"), QStringLiteral("जोड़ें"), QStringLiteral("हटाएँ"), QStringLiteral("नाम बदलें"), QStringLiteral("छिपाएँ"), QStringLiteral("लॉक करें"), QStringLiteral("ऊपर ले जाएँ"), QStringLiteral("नीचे ले जाएँ"), QStringLiteral("अनुमान लगाएँ"), QStringLiteral("भेजें"), QStringLiteral("माइक्रोफ़ोन चालू करें"), QStringLiteral("माइक्रोफ़ोन बंद"), QStringLiteral("अपना अनुमान लिखें"), QStringLiteral("कमरे में संदेश भेजें"), QStringLiteral("कमरा छोड़ें"), QStringLiteral("खिलाड़ियों की प्रतीक्षा")}}
	};
	const int controlIndex = controlSources.indexOf(source);
	if (controlIndex >= 0) {
		const QStringList words = controlTranslations.value(languageCode);
		if (controlIndex < words.size())
			return words.at(controlIndex);
	}
	static const QStringList drawingToolSources{
		QStringLiteral("Select / move / scale"), QStringLiteral("Pen"),
		QStringLiteral("Eraser · cut strokes"), QStringLiteral("Object Eraser"),
		QStringLiteral("Rectangle"), QStringLiteral("Ellipse"), QStringLiteral("Line"), QStringLiteral("Text")};
	static const QHash<QString, QStringList> drawingToolTranslations{
		{QStringLiteral("es"), {QStringLiteral("Seleccionar / mover / escalar"), QStringLiteral("Lápiz"), QStringLiteral("Borrador · cortar trazos"), QStringLiteral("Borrador de objetos"), QStringLiteral("Rectángulo"), QStringLiteral("Elipse"), QStringLiteral("Línea"), QStringLiteral("Texto")}},
		{QStringLiteral("fr"), {QStringLiteral("Sélectionner / déplacer / redimensionner"), QStringLiteral("Crayon"), QStringLiteral("Gomme · couper les traits"), QStringLiteral("Gomme d’objets"), QStringLiteral("Rectangle"), QStringLiteral("Ellipse"), QStringLiteral("Ligne"), QStringLiteral("Texte")}},
		{QStringLiteral("de"), {QStringLiteral("Auswählen / bewegen / skalieren"), QStringLiteral("Stift"), QStringLiteral("Radierer · Striche schneiden"), QStringLiteral("Objektradierer"), QStringLiteral("Rechteck"), QStringLiteral("Ellipse"), QStringLiteral("Linie"), QStringLiteral("Text")}},
		{QStringLiteral("pt"), {QStringLiteral("Selecionar / mover / redimensionar"), QStringLiteral("Caneta"), QStringLiteral("Borracha · cortar traços"), QStringLiteral("Borracha de objetos"), QStringLiteral("Retângulo"), QStringLiteral("Elipse"), QStringLiteral("Linha"), QStringLiteral("Texto")}},
		{QStringLiteral("it"), {QStringLiteral("Seleziona / sposta / ridimensiona"), QStringLiteral("Penna"), QStringLiteral("Gomma · taglia tratti"), QStringLiteral("Gomma oggetti"), QStringLiteral("Rettangolo"), QStringLiteral("Ellisse"), QStringLiteral("Linea"), QStringLiteral("Testo")}},
		{QStringLiteral("ru"), {QStringLiteral("Выбор / перемещение / масштаб"), QStringLiteral("Кисть"), QStringLiteral("Ластик · стирать штрихи"), QStringLiteral("Ластик объектов"), QStringLiteral("Прямоугольник"), QStringLiteral("Эллипс"), QStringLiteral("Линия"), QStringLiteral("Текст")}},
		{QStringLiteral("zh"), {QStringLiteral("选择 / 移动 / 缩放"), QStringLiteral("画笔"), QStringLiteral("橡皮擦 · 删除笔画"), QStringLiteral("对象橡皮擦"), QStringLiteral("矩形"), QStringLiteral("椭圆"), QStringLiteral("直线"), QStringLiteral("文字")}},
		{QStringLiteral("ja"), {QStringLiteral("選択 / 移動 / 拡大縮小"), QStringLiteral("ペン"), QStringLiteral("消しゴム · 線を消す"), QStringLiteral("オブジェクト消しゴム"), QStringLiteral("長方形"), QStringLiteral("楕円"), QStringLiteral("線"), QStringLiteral("テキスト")}},
		{QStringLiteral("ko"), {QStringLiteral("선택 / 이동 / 크기 조절"), QStringLiteral("펜"), QStringLiteral("지우개 · 획 자르기"), QStringLiteral("개체 지우개"), QStringLiteral("직사각형"), QStringLiteral("타원"), QStringLiteral("선"), QStringLiteral("텍스트")}},
		{QStringLiteral("ar"), {QStringLiteral("تحديد / تحريك / تغيير الحجم"), QStringLiteral("قلم"), QStringLiteral("ممحاة · قص الخطوط"), QStringLiteral("ممحاة الكائنات"), QStringLiteral("مستطيل"), QStringLiteral("قطع ناقص"), QStringLiteral("خط"), QStringLiteral("نص")}},
		{QStringLiteral("hi"), {QStringLiteral("चुनें / खिसकाएँ / आकार बदलें"), QStringLiteral("पेन"), QStringLiteral("इरेज़र · स्ट्रोक काटें"), QStringLiteral("ऑब्जेक्ट इरेज़र"), QStringLiteral("आयत"), QStringLiteral("दीर्घवृत्त"), QStringLiteral("रेखा"), QStringLiteral("पाठ")}}
	};
	const int drawingToolIndex = drawingToolSources.indexOf(source);
	if (drawingToolIndex >= 0) {
		const QStringList tools = drawingToolTranslations.value(languageCode);
		if (drawingToolIndex < tools.size())
			return tools.at(drawingToolIndex);
	}
	static const QStringList statusSources{
		QStringLiteral("Connected to master"), QStringLiteral("Connecting to master…"),
		QStringLiteral("Master disconnected; reconnecting…"), QStringLiteral("Disconnected from the game server"),
		QStringLiteral("Could not connect to the selected room"), QStringLiteral("Round ended"),
		QStringLiteral("Microphone on"), QStringLiteral("Disable microphone"),
		QStringLiteral("No microphone device"), QStringLiteral("Unsupported microphone format"),
		QStringLiteral("Could not start microphone"), QStringLiteral("Microphone stopped unexpectedly"),
		QStringLiteral("No audio output device"), QStringLiteral("Unsupported audio output format"),
		QStringLiteral("Could not start audio playback"), QStringLiteral("You can hear room voice chat")};
	static const QHash<QString, QStringList> statusTranslations{
		{QStringLiteral("es"), {QStringLiteral("Conectado al servidor principal"), QStringLiteral("Conectando al servidor principal…"), QStringLiteral("Servidor principal desconectado; reconectando…"), QStringLiteral("Desconectado del servidor de juego"), QStringLiteral("No se pudo conectar a la sala seleccionada"), QStringLiteral("Ronda terminada"), QStringLiteral("Micrófono activado"), QStringLiteral("Desactivar micrófono"), QStringLiteral("No hay dispositivo de micrófono"), QStringLiteral("Formato de micrófono no compatible"), QStringLiteral("No se pudo iniciar el micrófono"), QStringLiteral("El micrófono se detuvo inesperadamente"), QStringLiteral("No hay dispositivo de salida de audio"), QStringLiteral("Formato de salida de audio no compatible"), QStringLiteral("No se pudo iniciar la reproducción de audio"), QStringLiteral("Puedes escuchar el chat de voz de la sala")}},
		{QStringLiteral("fr"), {QStringLiteral("Connecté au serveur principal"), QStringLiteral("Connexion au serveur principal…"), QStringLiteral("Serveur principal déconnecté ; reconnexion…"), QStringLiteral("Déconnecté du serveur de jeu"), QStringLiteral("Impossible de rejoindre le salon sélectionné"), QStringLiteral("Tour terminé"), QStringLiteral("Micro activé"), QStringLiteral("Désactiver le micro"), QStringLiteral("Aucun microphone détecté"), QStringLiteral("Format de microphone non pris en charge"), QStringLiteral("Impossible de démarrer le microphone"), QStringLiteral("Le microphone s’est arrêté de façon inattendue"), QStringLiteral("Aucune sortie audio détectée"), QStringLiteral("Format de sortie audio non pris en charge"), QStringLiteral("Impossible de démarrer la lecture audio"), QStringLiteral("Vous pouvez écouter le chat vocal du salon")}},
		{QStringLiteral("de"), {QStringLiteral("Mit dem Hauptserver verbunden"), QStringLiteral("Verbindung zum Hauptserver…"), QStringLiteral("Hauptserver getrennt; erneuter Verbindungsversuch…"), QStringLiteral("Verbindung zum Spielserver getrennt"), QStringLiteral("Verbindung zum ausgewählten Raum nicht möglich"), QStringLiteral("Runde beendet"), QStringLiteral("Mikrofon eingeschaltet"), QStringLiteral("Mikrofon ausschalten"), QStringLiteral("Kein Mikrofon gefunden"), QStringLiteral("Mikrofonformat wird nicht unterstützt"), QStringLiteral("Mikrofon konnte nicht gestartet werden"), QStringLiteral("Mikrofon unerwartet gestoppt"), QStringLiteral("Kein Audioausgabegerät gefunden"), QStringLiteral("Audioausgabeformat wird nicht unterstützt"), QStringLiteral("Audiowiedergabe konnte nicht gestartet werden"), QStringLiteral("Du kannst den Raum-Sprachchat hören")}},
		{QStringLiteral("pt"), {QStringLiteral("Conectado ao servidor principal"), QStringLiteral("Conectando ao servidor principal…"), QStringLiteral("Servidor principal desconectado; reconectando…"), QStringLiteral("Desconectado do servidor do jogo"), QStringLiteral("Não foi possível conectar à sala selecionada"), QStringLiteral("Rodada encerrada"), QStringLiteral("Microfone ligado"), QStringLiteral("Desativar microfone"), QStringLiteral("Nenhum microfone encontrado"), QStringLiteral("Formato de microfone não compatível"), QStringLiteral("Não foi possível iniciar o microfone"), QStringLiteral("Microfone parou inesperadamente"), QStringLiteral("Nenhuma saída de áudio encontrada"), QStringLiteral("Formato de saída de áudio não compatível"), QStringLiteral("Não foi possível iniciar a reprodução de áudio"), QStringLiteral("Você pode ouvir o chat de voz da sala")}},
		{QStringLiteral("it"), {QStringLiteral("Connesso al server principale"), QStringLiteral("Connessione al server principale…"), QStringLiteral("Server principale disconnesso; riconnessione…"), QStringLiteral("Disconnesso dal server di gioco"), QStringLiteral("Impossibile connettersi alla stanza selezionata"), QStringLiteral("Round terminato"), QStringLiteral("Microfono attivo"), QStringLiteral("Disattiva microfono"), QStringLiteral("Nessun microfono trovato"), QStringLiteral("Formato microfono non supportato"), QStringLiteral("Impossibile avviare il microfono"), QStringLiteral("Microfono arrestato inaspettatamente"), QStringLiteral("Nessuna uscita audio trovata"), QStringLiteral("Formato audio non supportato"), QStringLiteral("Impossibile avviare la riproduzione audio"), QStringLiteral("Puoi ascoltare la chat vocale della stanza")}},
		{QStringLiteral("ru"), {QStringLiteral("Подключено к главному серверу"), QStringLiteral("Подключение к главному серверу…"), QStringLiteral("Главный сервер отключён; повторное подключение…"), QStringLiteral("Отключено от игрового сервера"), QStringLiteral("Не удалось подключиться к выбранной комнате"), QStringLiteral("Раунд завершён"), QStringLiteral("Микрофон включён"), QStringLiteral("Выключить микрофон"), QStringLiteral("Микрофон не найден"), QStringLiteral("Формат микрофона не поддерживается"), QStringLiteral("Не удалось запустить микрофон"), QStringLiteral("Микрофон неожиданно остановился"), QStringLiteral("Аудиовыход не найден"), QStringLiteral("Формат аудиовыхода не поддерживается"), QStringLiteral("Не удалось начать воспроизведение аудио"), QStringLiteral("Вы можете слышать голосовой чат комнаты")}},
		{QStringLiteral("zh"), {QStringLiteral("已连接到主服务器"), QStringLiteral("正在连接主服务器…"), QStringLiteral("主服务器已断开；正在重新连接…"), QStringLiteral("已与游戏服务器断开连接"), QStringLiteral("无法连接到所选房间"), QStringLiteral("回合结束"), QStringLiteral("麦克风已开启"), QStringLiteral("关闭麦克风"), QStringLiteral("未找到麦克风设备"), QStringLiteral("不支持的麦克风格式"), QStringLiteral("无法启动麦克风"), QStringLiteral("麦克风意外停止"), QStringLiteral("未找到音频输出设备"), QStringLiteral("不支持的音频输出格式"), QStringLiteral("无法启动音频播放"), QStringLiteral("你可以收听房间语音聊天")}},
		{QStringLiteral("ja"), {QStringLiteral("マスターサーバーに接続しました"), QStringLiteral("マスターサーバーに接続中…"), QStringLiteral("マスターサーバーが切断されました。再接続中…"), QStringLiteral("ゲームサーバーから切断されました"), QStringLiteral("選択したルームに接続できません"), QStringLiteral("ラウンド終了"), QStringLiteral("マイクオン"), QStringLiteral("マイクを無効化"), QStringLiteral("マイクデバイスがありません"), QStringLiteral("マイク形式に対応していません"), QStringLiteral("マイクを起動できません"), QStringLiteral("マイクが予期せず停止しました"), QStringLiteral("音声出力デバイスがありません"), QStringLiteral("音声出力形式に対応していません"), QStringLiteral("音声を再生できません"), QStringLiteral("ルームのボイスチャットを聞けます")}},
		{QStringLiteral("ko"), {QStringLiteral("마스터 서버에 연결됨"), QStringLiteral("마스터 서버에 연결 중…"), QStringLiteral("마스터 서버 연결 끊김; 다시 연결 중…"), QStringLiteral("게임 서버 연결이 끊김"), QStringLiteral("선택한 방에 연결할 수 없음"), QStringLiteral("라운드 종료"), QStringLiteral("마이크 켜짐"), QStringLiteral("마이크 끄기"), QStringLiteral("마이크 장치를 찾을 수 없음"), QStringLiteral("지원하지 않는 마이크 형식"), QStringLiteral("마이크를 시작할 수 없음"), QStringLiteral("마이크가 예기치 않게 중지됨"), QStringLiteral("오디오 출력 장치를 찾을 수 없음"), QStringLiteral("지원하지 않는 오디오 출력 형식"), QStringLiteral("오디오 재생을 시작할 수 없음"), QStringLiteral("방 음성 채팅을 들을 수 있습니다")}},
		{QStringLiteral("ar"), {QStringLiteral("متصل بالخادم الرئيسي"), QStringLiteral("جارٍ الاتصال بالخادم الرئيسي…"), QStringLiteral("انقطع اتصال الخادم الرئيسي؛ جارٍ إعادة الاتصال…"), QStringLiteral("انقطع الاتصال بخادم اللعبة"), QStringLiteral("تعذر الاتصال بالغرفة المحددة"), QStringLiteral("انتهت الجولة"), QStringLiteral("الميكروفون يعمل"), QStringLiteral("إيقاف الميكروفون"), QStringLiteral("لا يوجد جهاز ميكروفون"), QStringLiteral("تنسيق الميكروفون غير مدعوم"), QStringLiteral("تعذر تشغيل الميكروفون"), QStringLiteral("توقف الميكروفون بشكل غير متوقع"), QStringLiteral("لا يوجد جهاز إخراج صوتي"), QStringLiteral("تنسيق إخراج الصوت غير مدعوم"), QStringLiteral("تعذر بدء تشغيل الصوت"), QStringLiteral("يمكنك سماع الدردشة الصوتية للغرفة")}},
		{QStringLiteral("hi"), {QStringLiteral("मुख्य सर्वर से जुड़ा"), QStringLiteral("मुख्य सर्वर से जुड़ रहा है…"), QStringLiteral("मुख्य सर्वर डिस्कनेक्ट हुआ; फिर से जुड़ रहा है…"), QStringLiteral("गेम सर्वर से डिस्कनेक्ट हुआ"), QStringLiteral("चुने हुए कमरे से कनेक्ट नहीं हो सका"), QStringLiteral("राउंड समाप्त"), QStringLiteral("माइक्रोफ़ोन चालू"), QStringLiteral("माइक्रोफ़ोन बंद करें"), QStringLiteral("माइक्रोफ़ोन डिवाइस नहीं मिला"), QStringLiteral("माइक्रोफ़ोन फ़ॉर्मैट समर्थित नहीं है"), QStringLiteral("माइक्रोफ़ोन शुरू नहीं हो सका"), QStringLiteral("माइक्रोफ़ोन अप्रत्याशित रूप से रुक गया"), QStringLiteral("ऑडियो आउटपुट डिवाइस नहीं मिला"), QStringLiteral("ऑडियो आउटपुट फ़ॉर्मैट समर्थित नहीं है"), QStringLiteral("ऑडियो प्लेबैक शुरू नहीं हो सका"), QStringLiteral("आप कमरे की वॉइस चैट सुन सकते हैं")}}
	};
	const int statusIndex = statusSources.indexOf(source);
	if (statusIndex >= 0) {
		const QStringList statuses = statusTranslations.value(languageCode);
		if (statusIndex < statuses.size())
			return statuses.at(statusIndex);
	}
	static const QHash<QString, QHash<QString, QString>> extraLabels{
		{QStringLiteral("Brush / eraser size"), {{QStringLiteral("es"), QStringLiteral("Tamaño del pincel / borrador")}, {QStringLiteral("fr"), QStringLiteral("Taille du pinceau / de la gomme")}, {QStringLiteral("de"), QStringLiteral("Pinsel- / Radierergröße")}, {QStringLiteral("pt"), QStringLiteral("Tamanho do pincel / borracha")}, {QStringLiteral("it"), QStringLiteral("Dimensione pennello / gomma")}, {QStringLiteral("ru"), QStringLiteral("Размер кисти / ластика")}, {QStringLiteral("zh"), QStringLiteral("画笔 / 橡皮擦大小")}, {QStringLiteral("ja"), QStringLiteral("ブラシ / 消しゴムのサイズ")}, {QStringLiteral("ko"), QStringLiteral("브러시 / 지우개 크기")}, {QStringLiteral("ar"), QStringLiteral("حجم الفرشاة / الممحاة")}, {QStringLiteral("hi"), QStringLiteral("ब्रश / इरेज़र आकार")}}},
		{QStringLiteral("Clear Selected Layer"), {{QStringLiteral("es"), QStringLiteral("Borrar capa seleccionada")}, {QStringLiteral("fr"), QStringLiteral("Effacer le calque sélectionné")}, {QStringLiteral("de"), QStringLiteral("Ausgewählte Ebene leeren")}, {QStringLiteral("pt"), QStringLiteral("Limpar camada selecionada")}, {QStringLiteral("it"), QStringLiteral("Cancella livello selezionato")}, {QStringLiteral("ru"), QStringLiteral("Очистить выбранный слой")}, {QStringLiteral("zh"), QStringLiteral("清除所选图层")}, {QStringLiteral("ja"), QStringLiteral("選択したレイヤーを消去")}, {QStringLiteral("ko"), QStringLiteral("선택한 레이어 지우기")}, {QStringLiteral("ar"), QStringLiteral("مسح الطبقة المحددة")}, {QStringLiteral("hi"), QStringLiteral("चुनी हुई परत साफ़ करें")}}},
		{QStringLiteral("Clear All Layers"), {{QStringLiteral("es"), QStringLiteral("Borrar todas las capas")}, {QStringLiteral("fr"), QStringLiteral("Effacer tous les calques")}, {QStringLiteral("de"), QStringLiteral("Alle Ebenen leeren")}, {QStringLiteral("pt"), QStringLiteral("Limpar todas as camadas")}, {QStringLiteral("it"), QStringLiteral("Cancella tutti i livelli")}, {QStringLiteral("ru"), QStringLiteral("Очистить все слои")}, {QStringLiteral("zh"), QStringLiteral("清除所有图层")}, {QStringLiteral("ja"), QStringLiteral("すべてのレイヤーを消去")}, {QStringLiteral("ko"), QStringLiteral("모든 레이어 지우기")}, {QStringLiteral("ar"), QStringLiteral("مسح كل الطبقات")}, {QStringLiteral("hi"), QStringLiteral("सभी परतें साफ़ करें")}}},
		{QStringLiteral("Layers · bottom to top"), {{QStringLiteral("es"), QStringLiteral("Capas · de abajo hacia arriba")}, {QStringLiteral("fr"), QStringLiteral("Calques · du bas vers le haut")}, {QStringLiteral("de"), QStringLiteral("Ebenen · von unten nach oben")}, {QStringLiteral("pt"), QStringLiteral("Camadas · de baixo para cima")}, {QStringLiteral("it"), QStringLiteral("Livelli · dal basso verso l’alto")}, {QStringLiteral("ru"), QStringLiteral("Слои · снизу вверх")}, {QStringLiteral("zh"), QStringLiteral("图层 · 从下到上")}, {QStringLiteral("ja"), QStringLiteral("レイヤー · 下から上")}, {QStringLiteral("ko"), QStringLiteral("레이어 · 아래에서 위로")}, {QStringLiteral("ar"), QStringLiteral("الطبقات · من الأسفل إلى الأعلى")}, {QStringLiteral("hi"), QStringLiteral("परतें · नीचे से ऊपर")}}},
		{QStringLiteral("Players and spectators"), {{QStringLiteral("es"), QStringLiteral("Jugadores y espectadores")}, {QStringLiteral("fr"), QStringLiteral("Joueurs et spectateurs")}, {QStringLiteral("de"), QStringLiteral("Spieler und Zuschauer")}, {QStringLiteral("pt"), QStringLiteral("Jogadores e espectadores")}, {QStringLiteral("it"), QStringLiteral("Giocatori e spettatori")}, {QStringLiteral("ru"), QStringLiteral("Игроки и зрители")}, {QStringLiteral("zh"), QStringLiteral("玩家和观众")}, {QStringLiteral("ja"), QStringLiteral("プレイヤーと観戦者")}, {QStringLiteral("ko"), QStringLiteral("플레이어 및 관전자")}, {QStringLiteral("ar"), QStringLiteral("اللاعبون والمشاهدون")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ी और दर्शक")}}},
		{QStringLiteral("Room chat and guesses"), {{QStringLiteral("es"), QStringLiteral("Chat y respuestas de la sala")}, {QStringLiteral("fr"), QStringLiteral("Chat et réponses du salon")}, {QStringLiteral("de"), QStringLiteral("Raumchat und Tipps")}, {QStringLiteral("pt"), QStringLiteral("Chat e palpites da sala")}, {QStringLiteral("it"), QStringLiteral("Chat e tentativi della stanza")}, {QStringLiteral("ru"), QStringLiteral("Чат комнаты и ответы")}, {QStringLiteral("zh"), QStringLiteral("房间聊天和猜词")}, {QStringLiteral("ja"), QStringLiteral("ルームチャットと回答")}, {QStringLiteral("ko"), QStringLiteral("방 채팅 및 추측")}, {QStringLiteral("ar"), QStringLiteral("دردشة الغرفة والتخمينات")}, {QStringLiteral("hi"), QStringLiteral("कमरे की चैट और अनुमान")}}},
		{QStringLiteral("Progress toward the front of the player queue"), {{QStringLiteral("es"), QStringLiteral("Progreso hacia el inicio de la cola de jugadores")}, {QStringLiteral("fr"), QStringLiteral("Progression vers le début de la file des joueurs")}, {QStringLiteral("de"), QStringLiteral("Fortschritt bis zum Anfang der Spielerschlange")}, {QStringLiteral("pt"), QStringLiteral("Progresso até o início da fila de jogadores")}, {QStringLiteral("it"), QStringLiteral("Avanzamento verso l’inizio della coda dei giocatori")}, {QStringLiteral("ru"), QStringLiteral("Прогресс к началу очереди игроков")}, {QStringLiteral("zh"), QStringLiteral("玩家队列前进进度")}, {QStringLiteral("ja"), QStringLiteral("プレイヤー待ち列の先頭までの進行状況")}, {QStringLiteral("ko"), QStringLiteral("플레이어 대기열 앞쪽으로의 진행 상황")}, {QStringLiteral("ar"), QStringLiteral("التقدم نحو مقدمة قائمة انتظار اللاعبين")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ियों की कतार के आगे बढ़ने की प्रगति")}}},
		{QStringLiteral("The same size controls pen width and the stroke eraser radius."), {{QStringLiteral("es"), QStringLiteral("El mismo tamaño controla el ancho del lápiz y el radio del borrador de trazos.")}, {QStringLiteral("fr"), QStringLiteral("La même taille règle la largeur du crayon et le rayon de la gomme.")}, {QStringLiteral("de"), QStringLiteral("Dieselbe Größe steuert Stiftbreite und Radiererradius.")}, {QStringLiteral("pt"), QStringLiteral("O mesmo tamanho controla a largura da caneta e o raio da borracha.")}, {QStringLiteral("it"), QStringLiteral("La stessa dimensione regola la larghezza della penna e il raggio della gomma.")}, {QStringLiteral("ru"), QStringLiteral("Один размер задаёт ширину пера и радиус ластика.")}, {QStringLiteral("zh"), QStringLiteral("相同的尺寸设置控制画笔宽度和橡皮擦半径。")}, {QStringLiteral("ja"), QStringLiteral("同じサイズ設定でペン幅と消しゴムの半径を調整します。")}, {QStringLiteral("ko"), QStringLiteral("같은 크기 설정으로 펜 너비와 지우개 반경을 조절합니다.")}, {QStringLiteral("ar"), QStringLiteral("يتحكم الحجم نفسه في عرض القلم ونصف قطر الممحاة.")}, {QStringLiteral("hi"), QStringLiteral("एक ही आकार पेन की चौड़ाई और इरेज़र की त्रिज्या नियंत्रित करता है।")}}},
		{QStringLiteral("Show"), {{QStringLiteral("es"), QStringLiteral("Mostrar")}, {QStringLiteral("fr"), QStringLiteral("Afficher")}, {QStringLiteral("de"), QStringLiteral("Anzeigen")}, {QStringLiteral("pt"), QStringLiteral("Mostrar")}, {QStringLiteral("it"), QStringLiteral("Mostra")}, {QStringLiteral("ru"), QStringLiteral("Показать")}, {QStringLiteral("zh"), QStringLiteral("显示")}, {QStringLiteral("ja"), QStringLiteral("表示")}, {QStringLiteral("ko"), QStringLiteral("표시")}, {QStringLiteral("ar"), QStringLiteral("إظهار")}, {QStringLiteral("hi"), QStringLiteral("दिखाएँ")}}},
		{QStringLiteral("Unlock"), {{QStringLiteral("es"), QStringLiteral("Desbloquear")}, {QStringLiteral("fr"), QStringLiteral("Déverrouiller")}, {QStringLiteral("de"), QStringLiteral("Entsperren")}, {QStringLiteral("pt"), QStringLiteral("Desbloquear")}, {QStringLiteral("it"), QStringLiteral("Sblocca")}, {QStringLiteral("ru"), QStringLiteral("Разблокировать")}, {QStringLiteral("zh"), QStringLiteral("解锁")}, {QStringLiteral("ja"), QStringLiteral("ロック解除")}, {QStringLiteral("ko"), QStringLiteral("잠금 해제")}, {QStringLiteral("ar"), QStringLiteral("إلغاء القفل")}, {QStringLiteral("hi"), QStringLiteral("अनलॉक करें")}}},
		{QStringLiteral("Join as Player"), {{QStringLiteral("es"), QStringLiteral("Unirse como jugador")}, {QStringLiteral("fr"), QStringLiteral("Rejoindre comme joueur")}, {QStringLiteral("de"), QStringLiteral("Als Spieler beitreten")}, {QStringLiteral("pt"), QStringLiteral("Entrar como jogador")}, {QStringLiteral("it"), QStringLiteral("Entra come giocatore")}, {QStringLiteral("ru"), QStringLiteral("Войти как игрок")}, {QStringLiteral("zh"), QStringLiteral("以玩家身份加入")}, {QStringLiteral("ja"), QStringLiteral("プレイヤーとして参加")}, {QStringLiteral("ko"), QStringLiteral("플레이어로 참가")}, {QStringLiteral("ar"), QStringLiteral("انضم كلاعب")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ी के रूप में जुड़ें")}}},
		{QStringLiteral("Switch to Spectator"), {{QStringLiteral("es"), QStringLiteral("Cambiar a espectador")}, {QStringLiteral("fr"), QStringLiteral("Passer en spectateur")}, {QStringLiteral("de"), QStringLiteral("Zum Zuschauer wechseln")}, {QStringLiteral("pt"), QStringLiteral("Mudar para espectador")}, {QStringLiteral("it"), QStringLiteral("Passa a spettatore")}, {QStringLiteral("ru"), QStringLiteral("Перейти в режим зрителя")}, {QStringLiteral("zh"), QStringLiteral("切换为观众")}, {QStringLiteral("ja"), QStringLiteral("観戦者に切り替え")}, {QStringLiteral("ko"), QStringLiteral("관전자로 전환")}, {QStringLiteral("ar"), QStringLiteral("التبديل إلى وضع المشاهد")}, {QStringLiteral("hi"), QStringLiteral("दर्शक पर स्विच करें")}}},
		{QStringLiteral("Cancel Wait"), {{QStringLiteral("es"), QStringLiteral("Cancelar espera")}, {QStringLiteral("fr"), QStringLiteral("Annuler l’attente")}, {QStringLiteral("de"), QStringLiteral("Warten abbrechen")}, {QStringLiteral("pt"), QStringLiteral("Cancelar espera")}, {QStringLiteral("it"), QStringLiteral("Annulla attesa")}, {QStringLiteral("ru"), QStringLiteral("Отменить ожидание")}, {QStringLiteral("zh"), QStringLiteral("取消等待")}, {QStringLiteral("ja"), QStringLiteral("待機をキャンセル")}, {QStringLiteral("ko"), QStringLiteral("대기 취소")}, {QStringLiteral("ar"), QStringLiteral("إلغاء الانتظار")}, {QStringLiteral("hi"), QStringLiteral("प्रतीक्षा रद्द करें")}}},
		{QStringLiteral("Drawer can listen but not send"), {{QStringLiteral("es"), QStringLiteral("El dibujante puede escuchar, pero no enviar mensajes")}, {QStringLiteral("fr"), QStringLiteral("Le dessinateur peut écouter, mais pas envoyer de messages")}, {QStringLiteral("de"), QStringLiteral("Der Zeichner kann zuhören, aber keine Nachrichten senden")}, {QStringLiteral("pt"), QStringLiteral("O desenhista pode ouvir, mas não enviar mensagens")}, {QStringLiteral("it"), QStringLiteral("Il disegnatore può ascoltare, ma non inviare messaggi")}, {QStringLiteral("ru"), QStringLiteral("Рисующий может слушать, но не отправлять сообщения")}, {QStringLiteral("zh"), QStringLiteral("画手可以收听，但不能发送消息")}, {QStringLiteral("ja"), QStringLiteral("描画者は聞くことはできますが、メッセージは送信できません")}, {QStringLiteral("ko"), QStringLiteral("그림 담당자는 들을 수 있지만 메시지를 보낼 수 없습니다")}, {QStringLiteral("ar"), QStringLiteral("يمكن للرسام الاستماع، لكن لا يمكنه الإرسال")}, {QStringLiteral("hi"), QStringLiteral("चित्रकार सुन सकता है, लेकिन संदेश नहीं भेज सकता")}}},
		{QStringLiteral("Queue progress"), {{QStringLiteral("es"), QStringLiteral("Progreso de la cola")}, {QStringLiteral("fr"), QStringLiteral("Progression dans la file")}, {QStringLiteral("de"), QStringLiteral("Warteschlangenfortschritt")}, {QStringLiteral("pt"), QStringLiteral("Progresso da fila")}, {QStringLiteral("it"), QStringLiteral("Avanzamento della coda")}, {QStringLiteral("ru"), QStringLiteral("Прогресс очереди")}, {QStringLiteral("zh"), QStringLiteral("队列进度")}, {QStringLiteral("ja"), QStringLiteral("待ち列の進行状況")}, {QStringLiteral("ko"), QStringLiteral("대기열 진행 상황")}, {QStringLiteral("ar"), QStringLiteral("تقدم قائمة الانتظار")}, {QStringLiteral("hi"), QStringLiteral("कतार की प्रगति")}}},
		{QStringLiteral("3-24 letters, numbers, or underscores"), {{QStringLiteral("es"), QStringLiteral("3-24 letras, números o guiones bajos")}, {QStringLiteral("fr"), QStringLiteral("3 à 24 lettres, chiffres ou traits de soulignement")}, {QStringLiteral("de"), QStringLiteral("3–24 Buchstaben, Zahlen oder Unterstriche")}, {QStringLiteral("pt"), QStringLiteral("3 a 24 letras, números ou sublinhados")}, {QStringLiteral("it"), QStringLiteral("3-24 lettere, numeri o trattini bassi")}, {QStringLiteral("ru"), QStringLiteral("3–24 буквы, цифры или знаки подчёркивания")}, {QStringLiteral("zh"), QStringLiteral("3–24 个字母、数字或下划线")}, {QStringLiteral("ja"), QStringLiteral("3～24 文字の英字、数字、またはアンダースコア")}, {QStringLiteral("ko"), QStringLiteral("3~24자의 영문자, 숫자 또는 밑줄")}, {QStringLiteral("ar"), QStringLiteral("من 3 إلى 24 حرفًا أو رقمًا أو شرطة سفلية")}, {QStringLiteral("hi"), QStringLiteral("3–24 अक्षर, अंक या अंडरस्कोर")}}},
		{QStringLiteral("Shown to other players"), {{QStringLiteral("es"), QStringLiteral("Visible para otros jugadores")}, {QStringLiteral("fr"), QStringLiteral("Visible par les autres joueurs")}, {QStringLiteral("de"), QStringLiteral("Für andere Spieler sichtbar")}, {QStringLiteral("pt"), QStringLiteral("Visível para outros jogadores")}, {QStringLiteral("it"), QStringLiteral("Visibile agli altri giocatori")}, {QStringLiteral("ru"), QStringLiteral("Видно другим игрокам")}, {QStringLiteral("zh"), QStringLiteral("其他玩家可见")}, {QStringLiteral("ja"), QStringLiteral("他のプレイヤーに表示されます")}, {QStringLiteral("ko"), QStringLiteral("다른 플레이어에게 표시됩니다")}, {QStringLiteral("ar"), QStringLiteral("يظهر للاعبين الآخرين")}, {QStringLiteral("hi"), QStringLiteral("अन्य खिलाड़ियों को दिखाई देगा")}}},
		{QStringLiteral("hidden"), {{QStringLiteral("es"), QStringLiteral("oculta")}, {QStringLiteral("fr"), QStringLiteral("masqué")}, {QStringLiteral("de"), QStringLiteral("ausgeblendet")}, {QStringLiteral("pt"), QStringLiteral("oculta")}, {QStringLiteral("it"), QStringLiteral("nascosto")}, {QStringLiteral("ru"), QStringLiteral("скрыт")}, {QStringLiteral("zh"), QStringLiteral("隐藏")}, {QStringLiteral("ja"), QStringLiteral("非表示")}, {QStringLiteral("ko"), QStringLiteral("숨김")}, {QStringLiteral("ar"), QStringLiteral("مخفي")}, {QStringLiteral("hi"), QStringLiteral("छिपी")}}},
		{QStringLiteral("locked"), {{QStringLiteral("es"), QStringLiteral("bloqueada")}, {QStringLiteral("fr"), QStringLiteral("verrouillé")}, {QStringLiteral("de"), QStringLiteral("gesperrt")}, {QStringLiteral("pt"), QStringLiteral("bloqueada")}, {QStringLiteral("it"), QStringLiteral("bloccato")}, {QStringLiteral("ru"), QStringLiteral("заблокирован")}, {QStringLiteral("zh"), QStringLiteral("已锁定")}, {QStringLiteral("ja"), QStringLiteral("ロック中")}, {QStringLiteral("ko"), QStringLiteral("잠김")}, {QStringLiteral("ar"), QStringLiteral("مقفل")}, {QStringLiteral("hi"), QStringLiteral("लॉक की गई")}}},
		{QStringLiteral("Add layer"), {{QStringLiteral("es"), QStringLiteral("Añadir capa")}, {QStringLiteral("fr"), QStringLiteral("Ajouter un calque")}, {QStringLiteral("de"), QStringLiteral("Ebene hinzufügen")}, {QStringLiteral("pt"), QStringLiteral("Adicionar camada")}, {QStringLiteral("it"), QStringLiteral("Aggiungi livello")}, {QStringLiteral("ru"), QStringLiteral("Добавить слой")}, {QStringLiteral("zh"), QStringLiteral("添加图层")}, {QStringLiteral("ja"), QStringLiteral("レイヤーを追加")}, {QStringLiteral("ko"), QStringLiteral("레이어 추가")}, {QStringLiteral("ar"), QStringLiteral("إضافة طبقة")}, {QStringLiteral("hi"), QStringLiteral("परत जोड़ें")}}},
		{QStringLiteral("Rename layer"), {{QStringLiteral("es"), QStringLiteral("Cambiar nombre de capa")}, {QStringLiteral("fr"), QStringLiteral("Renommer le calque")}, {QStringLiteral("de"), QStringLiteral("Ebene umbenennen")}, {QStringLiteral("pt"), QStringLiteral("Renomear camada")}, {QStringLiteral("it"), QStringLiteral("Rinomina livello")}, {QStringLiteral("ru"), QStringLiteral("Переименовать слой")}, {QStringLiteral("zh"), QStringLiteral("重命名图层")}, {QStringLiteral("ja"), QStringLiteral("レイヤー名を変更")}, {QStringLiteral("ko"), QStringLiteral("레이어 이름 변경")}, {QStringLiteral("ar"), QStringLiteral("إعادة تسمية الطبقة")}, {QStringLiteral("hi"), QStringLiteral("परत का नाम बदलें")}}},
		{QStringLiteral("Layer name"), {{QStringLiteral("es"), QStringLiteral("Nombre de la capa")}, {QStringLiteral("fr"), QStringLiteral("Nom du calque")}, {QStringLiteral("de"), QStringLiteral("Ebenenname")}, {QStringLiteral("pt"), QStringLiteral("Nome da camada")}, {QStringLiteral("it"), QStringLiteral("Nome del livello")}, {QStringLiteral("ru"), QStringLiteral("Название слоя")}, {QStringLiteral("zh"), QStringLiteral("图层名称")}, {QStringLiteral("ja"), QStringLiteral("レイヤー名")}, {QStringLiteral("ko"), QStringLiteral("레이어 이름")}, {QStringLiteral("ar"), QStringLiteral("اسم الطبقة")}, {QStringLiteral("hi"), QStringLiteral("परत का नाम")}}},
		{QStringLiteral("New layer"), {{QStringLiteral("es"), QStringLiteral("Nueva capa")}, {QStringLiteral("fr"), QStringLiteral("Nouveau calque")}, {QStringLiteral("de"), QStringLiteral("Neue Ebene")}, {QStringLiteral("pt"), QStringLiteral("Nova camada")}, {QStringLiteral("it"), QStringLiteral("Nuovo livello")}, {QStringLiteral("ru"), QStringLiteral("Новый слой")}, {QStringLiteral("zh"), QStringLiteral("新建图层")}, {QStringLiteral("ja"), QStringLiteral("新しいレイヤー")}, {QStringLiteral("ko"), QStringLiteral("새 레이어")}, {QStringLiteral("ar"), QStringLiteral("طبقة جديدة")}, {QStringLiteral("hi"), QStringLiteral("नई परत")}}}
		,{QStringLiteral("Enter a username and password."), {{QStringLiteral("es"), QStringLiteral("Introduce un nombre de usuario y una contraseña.")}, {QStringLiteral("fr"), QStringLiteral("Saisissez un nom d’utilisateur et un mot de passe.")}, {QStringLiteral("de"), QStringLiteral("Gib einen Benutzernamen und ein Passwort ein.")}, {QStringLiteral("pt"), QStringLiteral("Digite um nome de usuário e uma senha.")}, {QStringLiteral("it"), QStringLiteral("Inserisci nome utente e password.")}, {QStringLiteral("ru"), QStringLiteral("Введите имя пользователя и пароль.")}, {QStringLiteral("zh"), QStringLiteral("请输入用户名和密码。")}, {QStringLiteral("ja"), QStringLiteral("ユーザー名とパスワードを入力してください。")}, {QStringLiteral("ko"), QStringLiteral("사용자 이름과 비밀번호를 입력하세요.")}, {QStringLiteral("ar"), QStringLiteral("أدخل اسم المستخدم وكلمة المرور.")}, {QStringLiteral("hi"), QStringLiteral("उपयोगकर्ता नाम और पासवर्ड दर्ज करें।")}}}
		,{QStringLiteral("Enter a display name for your new account."), {{QStringLiteral("es"), QStringLiteral("Introduce un nombre visible para tu nueva cuenta.")}, {QStringLiteral("fr"), QStringLiteral("Saisissez un nom affiché pour votre nouveau compte.")}, {QStringLiteral("de"), QStringLiteral("Gib einen Anzeigenamen für dein neues Konto ein.")}, {QStringLiteral("pt"), QStringLiteral("Digite um nome de exibição para sua nova conta.")}, {QStringLiteral("it"), QStringLiteral("Inserisci un nome visualizzato per il nuovo account.")}, {QStringLiteral("ru"), QStringLiteral("Введите отображаемое имя для новой учётной записи.")}, {QStringLiteral("zh"), QStringLiteral("请输入新账户的显示名称。")}, {QStringLiteral("ja"), QStringLiteral("新しいアカウントの表示名を入力してください。")}, {QStringLiteral("ko"), QStringLiteral("새 계정의 표시 이름을 입력하세요.")}, {QStringLiteral("ar"), QStringLiteral("أدخل اسم العرض لحسابك الجديد.")}, {QStringLiteral("hi"), QStringLiteral("अपने नए खाते के लिए प्रदर्शन नाम दर्ज करें।")}}}
		,{QStringLiteral("Contacting master…"), {{QStringLiteral("es"), QStringLiteral("Contactando con el servidor principal…")}, {QStringLiteral("fr"), QStringLiteral("Contact du serveur principal…")}, {QStringLiteral("de"), QStringLiteral("Hauptserver wird kontaktiert…")}, {QStringLiteral("pt"), QStringLiteral("Contatando o servidor principal…")}, {QStringLiteral("it"), QStringLiteral("Contatto del server principale…")}, {QStringLiteral("ru"), QStringLiteral("Связь с главным сервером…")}, {QStringLiteral("zh"), QStringLiteral("正在联系主服务器…")}, {QStringLiteral("ja"), QStringLiteral("マスターサーバーに問い合わせ中…")}, {QStringLiteral("ko"), QStringLiteral("마스터 서버에 연결 중…")}, {QStringLiteral("ar"), QStringLiteral("جارٍ الاتصال بالخادم الرئيسي…")}, {QStringLiteral("hi"), QStringLiteral("मुख्य सर्वर से संपर्क किया जा रहा है…")}}}
		,{QStringLiteral("Waiting for the master service connection…"), {{QStringLiteral("es"), QStringLiteral("Esperando la conexión con el servidor principal…")}, {QStringLiteral("fr"), QStringLiteral("En attente de la connexion au serveur principal…")}, {QStringLiteral("de"), QStringLiteral("Warte auf die Verbindung zum Hauptserver…")}, {QStringLiteral("pt"), QStringLiteral("Aguardando a conexão com o servidor principal…")}, {QStringLiteral("it"), QStringLiteral("In attesa della connessione al server principale…")}, {QStringLiteral("ru"), QStringLiteral("Ожидание подключения к главному серверу…")}, {QStringLiteral("zh"), QStringLiteral("正在等待连接主服务器…")}, {QStringLiteral("ja"), QStringLiteral("マスターサーバーへの接続を待っています…")}, {QStringLiteral("ko"), QStringLiteral("마스터 서버 연결 대기 중…")}, {QStringLiteral("ar"), QStringLiteral("بانتظار الاتصال بالخادم الرئيسي…")}, {QStringLiteral("hi"), QStringLiteral("मुख्य सर्वर कनेक्शन की प्रतीक्षा…")}}}
		,{QStringLiteral("Rooms are hosted by dedicated game-server processes."), {{QStringLiteral("es"), QStringLiteral("Las salas están alojadas en procesos de servidor de juego dedicados.")}, {QStringLiteral("fr"), QStringLiteral("Les salons sont hébergés par des processus dédiés de serveur de jeu.")}, {QStringLiteral("de"), QStringLiteral("Räume werden von eigenen Spielserver-Prozessen gehostet.")}, {QStringLiteral("pt"), QStringLiteral("As salas são hospedadas por processos dedicados de servidor de jogo.")}, {QStringLiteral("it"), QStringLiteral("Le stanze sono ospitate da processi server di gioco dedicati.")}, {QStringLiteral("ru"), QStringLiteral("Комнаты размещаются на выделенных игровых серверах.")}, {QStringLiteral("zh"), QStringLiteral("房间由专用游戏服务器进程托管。")}, {QStringLiteral("ja"), QStringLiteral("ルームは専用ゲームサーバープロセスでホストされます。")}, {QStringLiteral("ko"), QStringLiteral("방은 전용 게임 서버 프로세스에서 호스팅됩니다.")}, {QStringLiteral("ar"), QStringLiteral("تُستضاف الغرف بواسطة عمليات خادم ألعاب مخصصة.")}, {QStringLiteral("hi"), QStringLiteral("कमरे समर्पित गेम-सर्वर प्रक्रियाओं द्वारा होस्ट किए जाते हैं।")}}}
	};
	const QString extraText = extraLabels.value(source).value(languageCode);
	if (!extraText.isEmpty())
		return extraText;
	static const QStringList latencySources{QStringLiteral("unavailable"), QStringLiteral("excellent"),
		QStringLiteral("good"), QStringLiteral("fair"), QStringLiteral("weak"), QStringLiteral("Ping unavailable")};
	static const QHash<QString, QStringList> latencyTranslations{
		{QStringLiteral("es"), {QStringLiteral("no disponible"), QStringLiteral("excelente"), QStringLiteral("buena"), QStringLiteral("regular"), QStringLiteral("débil"), QStringLiteral("Latencia no disponible")}},
		{QStringLiteral("fr"), {QStringLiteral("indisponible"), QStringLiteral("excellente"), QStringLiteral("bonne"), QStringLiteral("moyenne"), QStringLiteral("faible"), QStringLiteral("Latence indisponible")}},
		{QStringLiteral("de"), {QStringLiteral("nicht verfügbar"), QStringLiteral("ausgezeichnet"), QStringLiteral("gut"), QStringLiteral("mittelmäßig"), QStringLiteral("schwach"), QStringLiteral("Latenz nicht verfügbar")}},
		{QStringLiteral("pt"), {QStringLiteral("indisponível"), QStringLiteral("excelente"), QStringLiteral("boa"), QStringLiteral("razoável"), QStringLiteral("fraca"), QStringLiteral("Latência indisponível")}},
		{QStringLiteral("it"), {QStringLiteral("non disponibile"), QStringLiteral("eccellente"), QStringLiteral("buona"), QStringLiteral("discreta"), QStringLiteral("debole"), QStringLiteral("Latenza non disponibile")}},
		{QStringLiteral("ru"), {QStringLiteral("недоступно"), QStringLiteral("отличная"), QStringLiteral("хорошая"), QStringLiteral("средняя"), QStringLiteral("слабая"), QStringLiteral("Задержка недоступна")}},
		{QStringLiteral("zh"), {QStringLiteral("不可用"), QStringLiteral("极佳"), QStringLiteral("良好"), QStringLiteral("一般"), QStringLiteral("较弱"), QStringLiteral("延迟不可用")}},
		{QStringLiteral("ja"), {QStringLiteral("利用不可"), QStringLiteral("非常に良好"), QStringLiteral("良好"), QStringLiteral("普通"), QStringLiteral("弱い"), QStringLiteral("遅延を取得できません")}},
		{QStringLiteral("ko"), {QStringLiteral("사용할 수 없음"), QStringLiteral("매우 좋음"), QStringLiteral("좋음"), QStringLiteral("보통"), QStringLiteral("약함"), QStringLiteral("지연 시간을 사용할 수 없음")}},
		{QStringLiteral("ar"), {QStringLiteral("غير متاح"), QStringLiteral("ممتاز"), QStringLiteral("جيد"), QStringLiteral("متوسط"), QStringLiteral("ضعيف"), QStringLiteral("زمن الاستجابة غير متاح")}},
		{QStringLiteral("hi"), {QStringLiteral("उपलब्ध नहीं"), QStringLiteral("उत्कृष्ट"), QStringLiteral("अच्छा"), QStringLiteral("ठीक"), QStringLiteral("कमज़ोर"), QStringLiteral("विलंब उपलब्ध नहीं")}}
	};
	const int latencyIndex = latencySources.indexOf(source);
	if (latencyIndex >= 0) {
		const QStringList qualityWords = latencyTranslations.value(languageCode);
		if (latencyIndex < qualityWords.size())
			return qualityWords.at(latencyIndex);
	}
	static const QHash<QString, QHash<QString, QString>> formattedTranslations{
		{QStringLiteral("Room: %1"), {{QStringLiteral("es"), QStringLiteral("Sala: %1")}, {QStringLiteral("fr"), QStringLiteral("Salon : %1")}, {QStringLiteral("de"), QStringLiteral("Raum: %1")}, {QStringLiteral("pt"), QStringLiteral("Sala: %1")}, {QStringLiteral("it"), QStringLiteral("Stanza: %1")}, {QStringLiteral("ru"), QStringLiteral("Комната: %1")}, {QStringLiteral("zh"), QStringLiteral("房间：%1")}, {QStringLiteral("ja"), QStringLiteral("ルーム：%1")}, {QStringLiteral("ko"), QStringLiteral("방: %1")}, {QStringLiteral("ar"), QStringLiteral("الغرفة: %1")}, {QStringLiteral("hi"), QStringLiteral("कमरा: %1")}}},
		{QStringLiteral("Connecting to %1…"), {{QStringLiteral("es"), QStringLiteral("Conectando a %1…")}, {QStringLiteral("fr"), QStringLiteral("Connexion à %1…")}, {QStringLiteral("de"), QStringLiteral("Verbindung zu %1…")}, {QStringLiteral("pt"), QStringLiteral("Conectando a %1…")}, {QStringLiteral("it"), QStringLiteral("Connessione a %1…")}, {QStringLiteral("ru"), QStringLiteral("Подключение к %1…")}, {QStringLiteral("zh"), QStringLiteral("正在连接到 %1…")}, {QStringLiteral("ja"), QStringLiteral("%1 に接続中…")}, {QStringLiteral("ko"), QStringLiteral("%1에 연결 중…")}, {QStringLiteral("ar"), QStringLiteral("جارٍ الاتصال بـ %1…")}, {QStringLiteral("hi"), QStringLiteral("%1 से कनेक्ट हो रहा है…")}}},
		{QStringLiteral("Signed in as %1"), {{QStringLiteral("es"), QStringLiteral("Sesión iniciada como %1")}, {QStringLiteral("fr"), QStringLiteral("Connecté en tant que %1")}, {QStringLiteral("de"), QStringLiteral("Angemeldet als %1")}, {QStringLiteral("pt"), QStringLiteral("Conectado como %1")}, {QStringLiteral("it"), QStringLiteral("Accesso effettuato come %1")}, {QStringLiteral("ru"), QStringLiteral("Выполнен вход: %1")}, {QStringLiteral("zh"), QStringLiteral("已登录为 %1")}, {QStringLiteral("ja"), QStringLiteral("%1 としてログイン中")}, {QStringLiteral("ko"), QStringLiteral("%1(으)로 로그인됨")}, {QStringLiteral("ar"), QStringLiteral("تم تسجيل الدخول باسم %1")}, {QStringLiteral("hi"), QStringLiteral("%1 के रूप में साइन इन")}}},
		{QStringLiteral("Signed in as %1 · %2 points"), {{QStringLiteral("es"), QStringLiteral("Sesión iniciada como %1 · %2 puntos")}, {QStringLiteral("fr"), QStringLiteral("Connecté en tant que %1 · %2 points")}, {QStringLiteral("de"), QStringLiteral("Angemeldet als %1 · %2 Punkte")}, {QStringLiteral("pt"), QStringLiteral("Conectado como %1 · %2 pontos")}, {QStringLiteral("it"), QStringLiteral("Accesso come %1 · %2 punti")}, {QStringLiteral("ru"), QStringLiteral("Вход: %1 · %2 очков")}, {QStringLiteral("zh"), QStringLiteral("已登录为 %1 · %2 分")}, {QStringLiteral("ja"), QStringLiteral("%1 としてログイン · %2 ポイント")}, {QStringLiteral("ko"), QStringLiteral("%1(으)로 로그인 · %2점")}, {QStringLiteral("ar"), QStringLiteral("تم تسجيل الدخول باسم %1 · %2 نقطة")}, {QStringLiteral("hi"), QStringLiteral("%1 के रूप में साइन इन · %2 अंक")}}},
		{QStringLiteral("Round in progress · drawing: %1"), {{QStringLiteral("es"), QStringLiteral("Ronda en curso · dibuja: %1")}, {QStringLiteral("fr"), QStringLiteral("Tour en cours · dessin : %1")}, {QStringLiteral("de"), QStringLiteral("Runde läuft · zeichnet: %1")}, {QStringLiteral("pt"), QStringLiteral("Rodada em andamento · desenhando: %1")}, {QStringLiteral("it"), QStringLiteral("Round in corso · disegna: %1")}, {QStringLiteral("ru"), QStringLiteral("Раунд идёт · рисует: %1")}, {QStringLiteral("zh"), QStringLiteral("回合进行中 · 绘画者：%1")}, {QStringLiteral("ja"), QStringLiteral("ラウンド進行中 · 描画中：%1")}, {QStringLiteral("ko"), QStringLiteral("라운드 진행 중 · 그리는 중: %1")}, {QStringLiteral("ar"), QStringLiteral("الجولة جارية · يرسم: %1")}, {QStringLiteral("hi"), QStringLiteral("राउंड जारी · चित्र बना रहे हैं: %1")}}}
		,{QStringLiteral("Your word: —"), {{QStringLiteral("es"), QStringLiteral("Tu palabra: —")}, {QStringLiteral("fr"), QStringLiteral("Votre mot : —")}, {QStringLiteral("de"), QStringLiteral("Dein Wort: —")}, {QStringLiteral("pt"), QStringLiteral("Sua palavra: —")}, {QStringLiteral("it"), QStringLiteral("La tua parola: —")}, {QStringLiteral("ru"), QStringLiteral("Ваше слово: —")}, {QStringLiteral("zh"), QStringLiteral("你的词语：—")}, {QStringLiteral("ja"), QStringLiteral("あなたの単語：—")}, {QStringLiteral("ko"), QStringLiteral("내 단어: —")}, {QStringLiteral("ar"), QStringLiteral("كلمتك: —")}, {QStringLiteral("hi"), QStringLiteral("आपका शब्द: —")}}}
		,{QStringLiteral("Your word: …"), {{QStringLiteral("es"), QStringLiteral("Tu palabra: …")}, {QStringLiteral("fr"), QStringLiteral("Votre mot : …")}, {QStringLiteral("de"), QStringLiteral("Dein Wort: …")}, {QStringLiteral("pt"), QStringLiteral("Sua palavra: …")}, {QStringLiteral("it"), QStringLiteral("La tua parola: …")}, {QStringLiteral("ru"), QStringLiteral("Ваше слово: …")}, {QStringLiteral("zh"), QStringLiteral("你的词语：…")}, {QStringLiteral("ja"), QStringLiteral("あなたの単語：…")}, {QStringLiteral("ko"), QStringLiteral("내 단어: …")}, {QStringLiteral("ar"), QStringLiteral("كلمتك: …")}, {QStringLiteral("hi"), QStringLiteral("आपका शब्द: …")}}}
		,{QStringLiteral("Your word: %1"), {{QStringLiteral("es"), QStringLiteral("Tu palabra: %1")}, {QStringLiteral("fr"), QStringLiteral("Votre mot : %1")}, {QStringLiteral("de"), QStringLiteral("Dein Wort: %1")}, {QStringLiteral("pt"), QStringLiteral("Sua palavra: %1")}, {QStringLiteral("it"), QStringLiteral("La tua parola: %1")}, {QStringLiteral("ru"), QStringLiteral("Ваше слово: %1")}, {QStringLiteral("zh"), QStringLiteral("你的词语：%1")}, {QStringLiteral("ja"), QStringLiteral("あなたの単語：%1")}, {QStringLiteral("ko"), QStringLiteral("내 단어: %1")}, {QStringLiteral("ar"), QStringLiteral("كلمتك: %1")}, {QStringLiteral("hi"), QStringLiteral("आपका शब्द: %1")}}}
		,{QStringLiteral("Answer: %1"), {{QStringLiteral("es"), QStringLiteral("Respuesta: %1")}, {QStringLiteral("fr"), QStringLiteral("Réponse : %1")}, {QStringLiteral("de"), QStringLiteral("Antwort: %1")}, {QStringLiteral("pt"), QStringLiteral("Resposta: %1")}, {QStringLiteral("it"), QStringLiteral("Risposta: %1")}, {QStringLiteral("ru"), QStringLiteral("Ответ: %1")}, {QStringLiteral("zh"), QStringLiteral("答案：%1")}, {QStringLiteral("ja"), QStringLiteral("答え：%1")}, {QStringLiteral("ko"), QStringLiteral("정답: %1")}, {QStringLiteral("ar"), QStringLiteral("الإجابة: %1")}, {QStringLiteral("hi"), QStringLiteral("उत्तर: %1")}}},
		{QStringLiteral("Cannot reach master at %1:%2"), {{QStringLiteral("es"), QStringLiteral("No se puede acceder al servidor principal en %1:%2")}, {QStringLiteral("fr"), QStringLiteral("Impossible de joindre le serveur principal à %1:%2")}, {QStringLiteral("de"), QStringLiteral("Hauptserver unter %1:%2 nicht erreichbar")}, {QStringLiteral("pt"), QStringLiteral("Não foi possível acessar o servidor principal em %1:%2")}, {QStringLiteral("it"), QStringLiteral("Impossibile raggiungere il server principale su %1:%2")}, {QStringLiteral("ru"), QStringLiteral("Не удаётся подключиться к главному серверу %1:%2")}, {QStringLiteral("zh"), QStringLiteral("无法连接到主服务器 %1:%2")}, {QStringLiteral("ja"), QStringLiteral("マスターサーバー %1:%2 に接続できません")}, {QStringLiteral("ko"), QStringLiteral("마스터 서버 %1:%2에 연결할 수 없습니다")}, {QStringLiteral("ar"), QStringLiteral("تعذر الوصول إلى الخادم الرئيسي على %1:%2")}, {QStringLiteral("hi"), QStringLiteral("मुख्य सर्वर %1:%2 तक पहुँचा नहीं जा सका")}}},
		{QStringLiteral("Could not save language setting: %1"), {{QStringLiteral("es"), QStringLiteral("No se pudo guardar el idioma: %1")}, {QStringLiteral("fr"), QStringLiteral("Impossible d’enregistrer la langue : %1")}, {QStringLiteral("de"), QStringLiteral("Spracheinstellung konnte nicht gespeichert werden: %1")}, {QStringLiteral("pt"), QStringLiteral("Não foi possível salvar o idioma: %1")}, {QStringLiteral("it"), QStringLiteral("Impossibile salvare la lingua: %1")}, {QStringLiteral("ru"), QStringLiteral("Не удалось сохранить язык: %1")}, {QStringLiteral("zh"), QStringLiteral("无法保存语言设置：%1")}, {QStringLiteral("ja"), QStringLiteral("言語設定を保存できませんでした：%1")}, {QStringLiteral("ko"), QStringLiteral("언어 설정을 저장할 수 없습니다: %1")}, {QStringLiteral("ar"), QStringLiteral("تعذر حفظ إعداد اللغة: %1")}, {QStringLiteral("hi"), QStringLiteral("भाषा सेटिंग सहेजी नहीं जा सकी: %1")}}},
		{QStringLiteral("Round %1 · %2 is drawing"), {{QStringLiteral("es"), QStringLiteral("Ronda %1 · dibuja %2")}, {QStringLiteral("fr"), QStringLiteral("Tour %1 · %2 dessine")}, {QStringLiteral("de"), QStringLiteral("Runde %1 · %2 zeichnet")}, {QStringLiteral("pt"), QStringLiteral("Rodada %1 · %2 está desenhando")}, {QStringLiteral("it"), QStringLiteral("Round %1 · %2 sta disegnando")}, {QStringLiteral("ru"), QStringLiteral("Раунд %1 · рисует %2")}, {QStringLiteral("zh"), QStringLiteral("第 %1 回合 · 绘画者：%2")}, {QStringLiteral("ja"), QStringLiteral("ラウンド %1 · 描画中：%2")}, {QStringLiteral("ko"), QStringLiteral("라운드 %1 · %2님이 그림을 그리고 있습니다")}, {QStringLiteral("ar"), QStringLiteral("الجولة %1 · يرسم %2")}, {QStringLiteral("hi"), QStringLiteral("राउंड %1 · %2 चित्र बना रहे हैं")}}},
		{QStringLiteral("%1 room(s) listed"), {{QStringLiteral("es"), QStringLiteral("Salas listadas: %1")}, {QStringLiteral("fr"), QStringLiteral("Salons affichés : %1")}, {QStringLiteral("de"), QStringLiteral("Angezeigte Räume: %1")}, {QStringLiteral("pt"), QStringLiteral("Salas listadas: %1")}, {QStringLiteral("it"), QStringLiteral("Stanze elencate: %1")}, {QStringLiteral("ru"), QStringLiteral("Комнат в списке: %1")}, {QStringLiteral("zh"), QStringLiteral("房间列表：%1")}, {QStringLiteral("ja"), QStringLiteral("表示中のルーム数：%1")}, {QStringLiteral("ko"), QStringLiteral("표시된 방: %1개")}, {QStringLiteral("ar"), QStringLiteral("الغرف المدرجة: %1")}, {QStringLiteral("hi"), QStringLiteral("सूचीबद्ध कमरे: %1")}}},
		{QStringLiteral("%1 room(s) listed · refreshed %2 second%3 ago"), {{QStringLiteral("es"), QStringLiteral("Salas: %1 · actualizado hace %2 s")}, {QStringLiteral("fr"), QStringLiteral("Salons : %1 · actualisés il y a %2 s")}, {QStringLiteral("de"), QStringLiteral("Räume: %1 · vor %2 s aktualisiert")}, {QStringLiteral("pt"), QStringLiteral("Salas: %1 · atualizado há %2 s")}, {QStringLiteral("it"), QStringLiteral("Stanze: %1 · aggiornate %2 s fa")}, {QStringLiteral("ru"), QStringLiteral("Комнат: %1 · обновлено %2 с назад")}, {QStringLiteral("zh"), QStringLiteral("房间：%1 · %2 秒前刷新")}, {QStringLiteral("ja"), QStringLiteral("ルーム：%1 · %2 秒前に更新")}, {QStringLiteral("ko"), QStringLiteral("방: %1 · %2초 전에 새로 고침")}, {QStringLiteral("ar"), QStringLiteral("الغرف: %1 · تم التحديث قبل %2 ثانية")}, {QStringLiteral("hi"), QStringLiteral("कमरे: %1 · %2 सेकंड पहले रीफ़्रेश")}}}
	};
	const QString formattedText = formattedTranslations.value(source).value(languageCode);
	if (!formattedText.isEmpty())
		return formattedText;
	static const QHash<QString, QHash<QString, QString>> eventTranslations{
		{QStringLiteral("Joined %1 as %2"), {{QStringLiteral("es"), QStringLiteral("Te uniste a %1 como %2")}, {QStringLiteral("fr"), QStringLiteral("Vous avez rejoint %1 comme %2")}, {QStringLiteral("de"), QStringLiteral("%1 als %2 beigetreten")}, {QStringLiteral("pt"), QStringLiteral("Você entrou em %1 como %2")}, {QStringLiteral("it"), QStringLiteral("Sei entrato in %1 come %2")}, {QStringLiteral("ru"), QStringLiteral("Вы вошли в %1 как %2")}, {QStringLiteral("zh"), QStringLiteral("已作为%2加入%1")}, {QStringLiteral("ja"), QStringLiteral("%1 に %2 として参加しました")}, {QStringLiteral("ko"), QStringLiteral("%2(으)로 %1에 참가했습니다")}, {QStringLiteral("ar"), QStringLiteral("انضممت إلى %1 بصفتك %2")}, {QStringLiteral("hi"), QStringLiteral("आप %2 के रूप में %1 में शामिल हुए")}}},
		{QStringLiteral("A player slot opened; you joined the players"), {{QStringLiteral("es"), QStringLiteral("Se liberó una plaza; te uniste a los jugadores")}, {QStringLiteral("fr"), QStringLiteral("Une place s’est libérée ; vous avez rejoint les joueurs")}, {QStringLiteral("de"), QStringLiteral("Ein Spielerplatz wurde frei; du bist den Spielern beigetreten")}, {QStringLiteral("pt"), QStringLiteral("Uma vaga abriu; você entrou para os jogadores")}, {QStringLiteral("it"), QStringLiteral("Si è liberato un posto; sei entrato tra i giocatori")}, {QStringLiteral("ru"), QStringLiteral("Освободилось место; вы присоединились к игрокам")}, {QStringLiteral("zh"), QStringLiteral("有玩家空位，你已加入玩家")}, {QStringLiteral("ja"), QStringLiteral("プレイヤー枠が空き、参加しました")}, {QStringLiteral("ko"), QStringLiteral("플레이어 자리가 생겨 참가했습니다")}, {QStringLiteral("ar"), QStringLiteral("توفر مقعد لاعب وانضممت إلى اللاعبين")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ी की जगह खाली हुई; आप खिलाड़ियों में शामिल हुए")}}},
		{QStringLiteral("You are waiting for a player slot"), {{QStringLiteral("es"), QStringLiteral("Estás esperando una plaza de jugador")}, {QStringLiteral("fr"), QStringLiteral("Vous attendez une place de joueur")}, {QStringLiteral("de"), QStringLiteral("Du wartest auf einen Spielerplatz")}, {QStringLiteral("pt"), QStringLiteral("Você está aguardando uma vaga de jogador")}, {QStringLiteral("it"), QStringLiteral("Sei in attesa di un posto da giocatore")}, {QStringLiteral("ru"), QStringLiteral("Вы ожидаете место игрока")}, {QStringLiteral("zh"), QStringLiteral("正在等待玩家席位")}, {QStringLiteral("ja"), QStringLiteral("プレイヤー枠を待っています")}, {QStringLiteral("ko"), QStringLiteral("플레이어 자리를 기다리는 중입니다")}, {QStringLiteral("ar"), QStringLiteral("أنت بانتظار مقعد لاعب")}, {QStringLiteral("hi"), QStringLiteral("आप खिलाड़ी की जगह की प्रतीक्षा कर रहे हैं")}}},
		{QStringLiteral("You are now spectating"), {{QStringLiteral("es"), QStringLiteral("Ahora estás mirando")}, {QStringLiteral("fr"), QStringLiteral("Vous êtes maintenant spectateur")}, {QStringLiteral("de"), QStringLiteral("Du bist jetzt Zuschauer")}, {QStringLiteral("pt"), QStringLiteral("Agora você está assistindo")}, {QStringLiteral("it"), QStringLiteral("Ora sei spettatore")}, {QStringLiteral("ru"), QStringLiteral("Теперь вы наблюдатель")}, {QStringLiteral("zh"), QStringLiteral("你现在是观众")}, {QStringLiteral("ja"), QStringLiteral("観戦中です")}, {QStringLiteral("ko"), QStringLiteral("이제 관전 중입니다")}, {QStringLiteral("ar"), QStringLiteral("أصبحت الآن مشاهدًا")}, {QStringLiteral("hi"), QStringLiteral("अब आप दर्शक हैं")}}},
		{QStringLiteral("You joined the players"), {{QStringLiteral("es"), QStringLiteral("Te uniste a los jugadores")}, {QStringLiteral("fr"), QStringLiteral("Vous avez rejoint les joueurs")}, {QStringLiteral("de"), QStringLiteral("Du bist den Spielern beigetreten")}, {QStringLiteral("pt"), QStringLiteral("Você entrou para os jogadores")}, {QStringLiteral("it"), QStringLiteral("Sei entrato tra i giocatori")}, {QStringLiteral("ru"), QStringLiteral("Вы присоединились к игрокам")}, {QStringLiteral("zh"), QStringLiteral("你已加入玩家")}, {QStringLiteral("ja"), QStringLiteral("プレイヤーとして参加しました")}, {QStringLiteral("ko"), QStringLiteral("플레이어로 참가했습니다")}, {QStringLiteral("ar"), QStringLiteral("انضممت إلى اللاعبين")}, {QStringLiteral("hi"), QStringLiteral("आप खिलाड़ियों में शामिल हुए")}}},
		{QStringLiteral("The word was %1"), {{QStringLiteral("es"), QStringLiteral("La palabra era %1")}, {QStringLiteral("fr"), QStringLiteral("Le mot était %1")}, {QStringLiteral("de"), QStringLiteral("Das Wort war %1")}, {QStringLiteral("pt"), QStringLiteral("A palavra era %1")}, {QStringLiteral("it"), QStringLiteral("La parola era %1")}, {QStringLiteral("ru"), QStringLiteral("Слово было: %1")}, {QStringLiteral("zh"), QStringLiteral("答案是：%1")}, {QStringLiteral("ja"), QStringLiteral("答えは %1 でした")}, {QStringLiteral("ko"), QStringLiteral("정답은 %1였습니다")}, {QStringLiteral("ar"), QStringLiteral("كانت الكلمة %1")}, {QStringLiteral("hi"), QStringLiteral("शब्द था: %1")}}},
		{QStringLiteral("%1 guessed correctly in place %2 (+%3 points)"), {{QStringLiteral("es"), QStringLiteral("%1 acertó en el puesto %2 (+%3 puntos)")}, {QStringLiteral("fr"), QStringLiteral("%1 a trouvé en position %2 (+%3 points)")}, {QStringLiteral("de"), QStringLiteral("%1 hat auf Platz %2 richtig geraten (+%3 Punkte)")}, {QStringLiteral("pt"), QStringLiteral("%1 acertou na posição %2 (+%3 pontos)")}, {QStringLiteral("it"), QStringLiteral("%1 ha indovinato al posto %2 (+%3 punti)")}, {QStringLiteral("ru"), QStringLiteral("%1 угадал на месте %2 (+%3 очков)")}, {QStringLiteral("zh"), QStringLiteral("%1 猜对，排名第 %2（+%3 分）")}, {QStringLiteral("ja"), QStringLiteral("%1 が %2 位で正解（+%3 ポイント）")}, {QStringLiteral("ko"), QStringLiteral("%1님이 %2위로 정답을 맞혔습니다(+%3점)")}, {QStringLiteral("ar"), QStringLiteral("%1 خمّن بشكل صحيح في المركز %2 (+%3 نقطة)")}, {QStringLiteral("hi"), QStringLiteral("%1 ने स्थान %2 पर सही अनुमान लगाया (+%3 अंक)")}}},
		{QStringLiteral("%1 left the room"), {{QStringLiteral("es"), QStringLiteral("%1 salió de la sala")}, {QStringLiteral("fr"), QStringLiteral("%1 a quitté le salon")}, {QStringLiteral("de"), QStringLiteral("%1 hat den Raum verlassen")}, {QStringLiteral("pt"), QStringLiteral("%1 saiu da sala")}, {QStringLiteral("it"), QStringLiteral("%1 ha lasciato la stanza")}, {QStringLiteral("ru"), QStringLiteral("%1 покинул комнату")}, {QStringLiteral("zh"), QStringLiteral("%1离开了房间")}, {QStringLiteral("ja"), QStringLiteral("%1 がルームを退出しました")}, {QStringLiteral("ko"), QStringLiteral("%1님이 방을 나갔습니다")}, {QStringLiteral("ar"), QStringLiteral("غادر %1 الغرفة")}, {QStringLiteral("hi"), QStringLiteral("%1 ने कमरा छोड़ दिया")}}},
		{QStringLiteral("Selected room has an invalid server address"), {{QStringLiteral("es"), QStringLiteral("La sala seleccionada tiene una dirección de servidor no válida")}, {QStringLiteral("fr"), QStringLiteral("L’adresse du serveur du salon sélectionné est invalide")}, {QStringLiteral("de"), QStringLiteral("Der ausgewählte Raum hat eine ungültige Serveradresse")}, {QStringLiteral("pt"), QStringLiteral("A sala selecionada tem um endereço de servidor inválido")}, {QStringLiteral("it"), QStringLiteral("La stanza selezionata ha un indirizzo server non valido")}, {QStringLiteral("ru"), QStringLiteral("У выбранной комнаты неверный адрес сервера")}, {QStringLiteral("zh"), QStringLiteral("所选房间的服务器地址无效")}, {QStringLiteral("ja"), QStringLiteral("選択したルームのサーバーアドレスが無効です")}, {QStringLiteral("ko"), QStringLiteral("선택한 방의 서버 주소가 잘못되었습니다")}, {QStringLiteral("ar"), QStringLiteral("عنوان الخادم للغرفة المحددة غير صالح")}, {QStringLiteral("hi"), QStringLiteral("चुने हुए कमरे का सर्वर पता अमान्य है")}}}
	};
	const QString eventText = eventTranslations.value(source).value(languageCode);
	if (!eventText.isEmpty())
		return eventText;
	static const QHash<QString, QHash<QString, QString>> roleTranslations{
		{QStringLiteral("player"), {{QStringLiteral("es"), QStringLiteral("jugador")}, {QStringLiteral("fr"), QStringLiteral("joueur")}, {QStringLiteral("de"), QStringLiteral("Spieler")}, {QStringLiteral("pt"), QStringLiteral("jogador")}, {QStringLiteral("it"), QStringLiteral("giocatore")}, {QStringLiteral("ru"), QStringLiteral("игрок")}, {QStringLiteral("zh"), QStringLiteral("玩家")}, {QStringLiteral("ja"), QStringLiteral("プレイヤー")}, {QStringLiteral("ko"), QStringLiteral("플레이어")}, {QStringLiteral("ar"), QStringLiteral("لاعب")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ी")}}},
		{QStringLiteral("spectator"), {{QStringLiteral("es"), QStringLiteral("espectador")}, {QStringLiteral("fr"), QStringLiteral("spectateur")}, {QStringLiteral("de"), QStringLiteral("Zuschauer")}, {QStringLiteral("pt"), QStringLiteral("espectador")}, {QStringLiteral("it"), QStringLiteral("spettatore")}, {QStringLiteral("ru"), QStringLiteral("зритель")}, {QStringLiteral("zh"), QStringLiteral("观众")}, {QStringLiteral("ja"), QStringLiteral("観戦者")}, {QStringLiteral("ko"), QStringLiteral("관전자")}, {QStringLiteral("ar"), QStringLiteral("مشاهد")}, {QStringLiteral("hi"), QStringLiteral("दर्शक")}}},
		{QStringLiteral("You were kicked out because someone else logged in to this account."), {{QStringLiteral("es"), QStringLiteral("Se te expulsó porque otra persona inició sesión en esta cuenta.")}, {QStringLiteral("fr"), QStringLiteral("Vous avez été déconnecté, car quelqu’un d’autre s’est connecté à ce compte.")}, {QStringLiteral("de"), QStringLiteral("Du wurdest abgemeldet, weil sich jemand anderes bei diesem Konto angemeldet hat.")}, {QStringLiteral("pt"), QStringLiteral("Você foi desconectado porque outra pessoa entrou nesta conta.")}, {QStringLiteral("it"), QStringLiteral("Sei stato disconnesso perché qualcun altro ha effettuato l’accesso a questo account.")}, {QStringLiteral("ru"), QStringLiteral("Вы были отключены, потому что кто-то другой вошёл в эту учётную запись.")}, {QStringLiteral("zh"), QStringLiteral("由于其他人登录了此账户，你已被强制退出。")}, {QStringLiteral("ja"), QStringLiteral("他のユーザーがこのアカウントにログインしたため、ログアウトされました。")}, {QStringLiteral("ko"), QStringLiteral("다른 사용자가 이 계정에 로그인하여 로그아웃되었습니다.")}, {QStringLiteral("ar"), QStringLiteral("تم تسجيل خروجك لأن شخصًا آخر سجّل الدخول إلى هذا الحساب.")}, {QStringLiteral("hi"), QStringLiteral("किसी अन्य व्यक्ति के इस खाते में साइन इन करने के कारण आपको बाहर कर दिया गया है।")}}}
	};
	const QString roleText = roleTranslations.value(source).value(languageCode);
	if (!roleText.isEmpty())
		return roleText;
	static const QHash<QString, QHash<QString, QString>> liveGameTranslations{
		{QStringLiteral("Room: %1 · Players: %2/%3 · Spectators: %4"), {{QStringLiteral("es"), QStringLiteral("Sala: %1 · Jugadores: %2/%3 · Espectadores: %4")}, {QStringLiteral("fr"), QStringLiteral("Salon : %1 · Joueurs : %2/%3 · Spectateurs : %4")}, {QStringLiteral("de"), QStringLiteral("Raum: %1 · Spieler: %2/%3 · Zuschauer: %4")}, {QStringLiteral("pt"), QStringLiteral("Sala: %1 · Jogadores: %2/%3 · Espectadores: %4")}, {QStringLiteral("it"), QStringLiteral("Stanza: %1 · Giocatori: %2/%3 · Spettatori: %4")}, {QStringLiteral("ru"), QStringLiteral("Комната: %1 · Игроки: %2/%3 · Зрители: %4")}, {QStringLiteral("zh"), QStringLiteral("房间：%1 · 玩家：%2/%3 · 观众：%4")}, {QStringLiteral("ja"), QStringLiteral("ルーム：%1 · プレイヤー：%2/%3 · 観戦者：%4")}, {QStringLiteral("ko"), QStringLiteral("방: %1 · 플레이어: %2/%3 · 관전자: %4")}, {QStringLiteral("ar"), QStringLiteral("الغرفة: %1 · اللاعبون: %2/%3 · المشاهدون: %4")}, {QStringLiteral("hi"), QStringLiteral("कमरा: %1 · खिलाड़ी: %2/%3 · दर्शक: %4")}}},
		{QStringLiteral("Waiting for a player slot · %1 ahead · position %2 of %3"), {{QStringLiteral("es"), QStringLiteral("Esperando una plaza · %1 delante · posición %2 de %3")}, {QStringLiteral("fr"), QStringLiteral("En attente d’une place · %1 devant · position %2 sur %3")}, {QStringLiteral("de"), QStringLiteral("Warte auf einen Platz · %1 voraus · Position %2 von %3")}, {QStringLiteral("pt"), QStringLiteral("Aguardando vaga · %1 à frente · posição %2 de %3")}, {QStringLiteral("it"), QStringLiteral("In attesa di un posto · %1 davanti · posizione %2 di %3")}, {QStringLiteral("ru"), QStringLiteral("Ожидание места · впереди %1 · позиция %2 из %3")}, {QStringLiteral("zh"), QStringLiteral("等待玩家席位 · 前方 %1 人 · 第 %2 位，共 %3")}, {QStringLiteral("ja"), QStringLiteral("プレイヤー枠を待機中 · 前に %1 人 · %3 人中 %2 番目")}, {QStringLiteral("ko"), QStringLiteral("플레이어 자리 대기 중 · 앞에 %1명 · %3명 중 %2번째")}, {QStringLiteral("ar"), QStringLiteral("بانتظار مقعد لاعب · أمامك %1 · الموضع %2 من %3")}, {QStringLiteral("hi"), QStringLiteral("खिलाड़ी की जगह की प्रतीक्षा · आगे %1 · %3 में से स्थान %2")}}},
		{QStringLiteral("%1 / %2 · %3 spectating"), {{QStringLiteral("es"), QStringLiteral("%1 / %2 · %3 mirando")}, {QStringLiteral("fr"), QStringLiteral("%1 / %2 · %3 spectateur(s)")}, {QStringLiteral("de"), QStringLiteral("%1 / %2 · %3 Zuschauer")}, {QStringLiteral("pt"), QStringLiteral("%1 / %2 · %3 assistindo")}, {QStringLiteral("it"), QStringLiteral("%1 / %2 · %3 spettatori")}, {QStringLiteral("ru"), QStringLiteral("%1 / %2 · зрителей: %3")}, {QStringLiteral("zh"), QStringLiteral("%1 / %2 · 观众：%3")}, {QStringLiteral("ja"), QStringLiteral("%1 / %2 · 観戦者 %3 人")}, {QStringLiteral("ko"), QStringLiteral("%1 / %2 · 관전자 %3명")}, {QStringLiteral("ar"), QStringLiteral("%1 / %2 · المشاهدون %3")}, {QStringLiteral("hi"), QStringLiteral("%1 / %2 · %3 दर्शक")}}},
		{QStringLiteral("%1 sec"), {{QStringLiteral("es"), QStringLiteral("%1 s")}, {QStringLiteral("fr"), QStringLiteral("%1 s")}, {QStringLiteral("de"), QStringLiteral("%1 Sek.")}, {QStringLiteral("pt"), QStringLiteral("%1 s")}, {QStringLiteral("it"), QStringLiteral("%1 s")}, {QStringLiteral("ru"), QStringLiteral("%1 с")}, {QStringLiteral("zh"), QStringLiteral("%1 秒")}, {QStringLiteral("ja"), QStringLiteral("%1 秒")}, {QStringLiteral("ko"), QStringLiteral("%1초")}, {QStringLiteral("ar"), QStringLiteral("%1 ث")}, {QStringLiteral("hi"), QStringLiteral("%1 सेकंड")}}},
		{QStringLiteral("Country not set"), {{QStringLiteral("es"), QStringLiteral("País sin configurar")}, {QStringLiteral("fr"), QStringLiteral("Pays non défini")}, {QStringLiteral("de"), QStringLiteral("Land nicht festgelegt")}, {QStringLiteral("pt"), QStringLiteral("País não definido")}, {QStringLiteral("it"), QStringLiteral("Paese non impostato")}, {QStringLiteral("ru"), QStringLiteral("Страна не указана")}, {QStringLiteral("zh"), QStringLiteral("未设置国家/地区")}, {QStringLiteral("ja"), QStringLiteral("国が未設定です")}, {QStringLiteral("ko"), QStringLiteral("국가가 설정되지 않음")}, {QStringLiteral("ar"), QStringLiteral("لم يتم تحديد الدولة")}, {QStringLiteral("hi"), QStringLiteral("देश सेट नहीं है")}}},
		{QStringLiteral("The same size controls pen width and the stroke eraser radius."), {{QStringLiteral("es"), QStringLiteral("El mismo tamaño controla el ancho del lápiz y el radio del borrador de trazos.")}, {QStringLiteral("fr"), QStringLiteral("La même taille règle la largeur du crayon et le rayon de la gomme des traits.")}, {QStringLiteral("de"), QStringLiteral("Dieselbe Größe bestimmt Stiftbreite und Radius des Strichradierers.")}, {QStringLiteral("pt"), QStringLiteral("O mesmo tamanho controla a largura da caneta e o raio da borracha de traços.")}, {QStringLiteral("it"), QStringLiteral("La stessa dimensione controlla la larghezza della penna e il raggio della gomma dei tratti.")}, {QStringLiteral("ru"), QStringLiteral("Один размер задаёт ширину пера и радиус ластика штрихов.")}, {QStringLiteral("zh"), QStringLiteral("相同的尺寸同时控制画笔宽度和笔画橡皮擦半径。")}, {QStringLiteral("ja"), QStringLiteral("同じサイズがペンの幅とストローク消しゴムの半径に適用されます。")}, {QStringLiteral("ko"), QStringLiteral("같은 크기가 펜 너비와 획 지우개 반경을 조절합니다.")}, {QStringLiteral("ar"), QStringLiteral("يتحكم الحجم نفسه في عرض القلم ونصف قطر ممحاة الخطوط.")}, {QStringLiteral("hi"), QStringLiteral("एक ही आकार पेन की चौड़ाई और स्ट्रोक इरेज़र की त्रिज्या नियंत्रित करता है।")}}},
		{QStringLiteral("Add layer"), {{QStringLiteral("es"), QStringLiteral("Añadir capa")}, {QStringLiteral("fr"), QStringLiteral("Ajouter un calque")}, {QStringLiteral("de"), QStringLiteral("Ebene hinzufügen")}, {QStringLiteral("pt"), QStringLiteral("Adicionar camada")}, {QStringLiteral("it"), QStringLiteral("Aggiungi livello")}, {QStringLiteral("ru"), QStringLiteral("Добавить слой")}, {QStringLiteral("zh"), QStringLiteral("添加图层")}, {QStringLiteral("ja"), QStringLiteral("レイヤーを追加")}, {QStringLiteral("ko"), QStringLiteral("레이어 추가")}, {QStringLiteral("ar"), QStringLiteral("إضافة طبقة")}, {QStringLiteral("hi"), QStringLiteral("परत जोड़ें")}}},
		{QStringLiteral("Rename layer"), {{QStringLiteral("es"), QStringLiteral("Cambiar nombre de capa")}, {QStringLiteral("fr"), QStringLiteral("Renommer le calque")}, {QStringLiteral("de"), QStringLiteral("Ebene umbenennen")}, {QStringLiteral("pt"), QStringLiteral("Renomear camada")}, {QStringLiteral("it"), QStringLiteral("Rinomina livello")}, {QStringLiteral("ru"), QStringLiteral("Переименовать слой")}, {QStringLiteral("zh"), QStringLiteral("重命名图层")}, {QStringLiteral("ja"), QStringLiteral("レイヤー名を変更")}, {QStringLiteral("ko"), QStringLiteral("레이어 이름 변경")}, {QStringLiteral("ar"), QStringLiteral("إعادة تسمية الطبقة")}, {QStringLiteral("hi"), QStringLiteral("परत का नाम बदलें")}}},
		{QStringLiteral("Layer name"), {{QStringLiteral("es"), QStringLiteral("Nombre de la capa")}, {QStringLiteral("fr"), QStringLiteral("Nom du calque")}, {QStringLiteral("de"), QStringLiteral("Ebenenname")}, {QStringLiteral("pt"), QStringLiteral("Nome da camada")}, {QStringLiteral("it"), QStringLiteral("Nome del livello")}, {QStringLiteral("ru"), QStringLiteral("Название слоя")}, {QStringLiteral("zh"), QStringLiteral("图层名称")}, {QStringLiteral("ja"), QStringLiteral("レイヤー名")}, {QStringLiteral("ko"), QStringLiteral("레이어 이름")}, {QStringLiteral("ar"), QStringLiteral("اسم الطبقة")}, {QStringLiteral("hi"), QStringLiteral("परत का नाम")}}},
		{QStringLiteral("New layer"), {{QStringLiteral("es"), QStringLiteral("Nueva capa")}, {QStringLiteral("fr"), QStringLiteral("Nouveau calque")}, {QStringLiteral("de"), QStringLiteral("Neue Ebene")}, {QStringLiteral("pt"), QStringLiteral("Nova camada")}, {QStringLiteral("it"), QStringLiteral("Nuovo livello")}, {QStringLiteral("ru"), QStringLiteral("Новый слой")}, {QStringLiteral("zh"), QStringLiteral("新图层")}, {QStringLiteral("ja"), QStringLiteral("新しいレイヤー")}, {QStringLiteral("ko"), QStringLiteral("새 레이어")}, {QStringLiteral("ar"), QStringLiteral("طبقة جديدة")}, {QStringLiteral("hi"), QStringLiteral("नई परत")}}},
		{QStringLiteral("Add text"), {{QStringLiteral("es"), QStringLiteral("Añadir texto")}, {QStringLiteral("fr"), QStringLiteral("Ajouter du texte")}, {QStringLiteral("de"), QStringLiteral("Text hinzufügen")}, {QStringLiteral("pt"), QStringLiteral("Adicionar texto")}, {QStringLiteral("it"), QStringLiteral("Aggiungi testo")}, {QStringLiteral("ru"), QStringLiteral("Добавить текст")}, {QStringLiteral("zh"), QStringLiteral("添加文字")}, {QStringLiteral("ja"), QStringLiteral("テキストを追加")}, {QStringLiteral("ko"), QStringLiteral("텍스트 추가")}, {QStringLiteral("ar"), QStringLiteral("إضافة نص")}, {QStringLiteral("hi"), QStringLiteral("पाठ जोड़ें")}}},
		{QStringLiteral("Text to place on the canvas"), {{QStringLiteral("es"), QStringLiteral("Texto para colocar en el lienzo")}, {QStringLiteral("fr"), QStringLiteral("Texte à placer sur la toile")}, {QStringLiteral("de"), QStringLiteral("Text für die Zeichenfläche")}, {QStringLiteral("pt"), QStringLiteral("Texto para colocar na tela")}, {QStringLiteral("it"), QStringLiteral("Testo da inserire sulla tela")}, {QStringLiteral("ru"), QStringLiteral("Текст для размещения на холсте")}, {QStringLiteral("zh"), QStringLiteral("要放置在画布上的文字")}, {QStringLiteral("ja"), QStringLiteral("キャンバスに配置するテキスト")}, {QStringLiteral("ko"), QStringLiteral("캔버스에 배치할 텍스트")}, {QStringLiteral("ar"), QStringLiteral("النص المراد وضعه على اللوحة")}, {QStringLiteral("hi"), QStringLiteral("कैनवास पर रखने के लिए पाठ")}}},
		{QStringLiteral("Choose drawing color"), {{QStringLiteral("es"), QStringLiteral("Elegir color de dibujo")}, {QStringLiteral("fr"), QStringLiteral("Choisir la couleur du dessin")}, {QStringLiteral("de"), QStringLiteral("Zeichenfarbe auswählen")}, {QStringLiteral("pt"), QStringLiteral("Escolher cor do desenho")}, {QStringLiteral("it"), QStringLiteral("Scegli il colore del disegno")}, {QStringLiteral("ru"), QStringLiteral("Выбрать цвет рисования")}, {QStringLiteral("zh"), QStringLiteral("选择绘图颜色")}, {QStringLiteral("ja"), QStringLiteral("描画色を選択")}, {QStringLiteral("ko"), QStringLiteral("그리기 색상 선택")}, {QStringLiteral("ar"), QStringLiteral("اختر لون الرسم")}, {QStringLiteral("hi"), QStringLiteral("ड्राइंग का रंग चुनें")}}},
		{QStringLiteral("Waiting for another player"), {{QStringLiteral("es"), QStringLiteral("Esperando a otro jugador")}, {QStringLiteral("fr"), QStringLiteral("En attente d’un autre joueur")}, {QStringLiteral("de"), QStringLiteral("Warte auf einen weiteren Spieler")}, {QStringLiteral("pt"), QStringLiteral("Aguardando outro jogador")}, {QStringLiteral("it"), QStringLiteral("In attesa di un altro giocatore")}, {QStringLiteral("ru"), QStringLiteral("Ожидание другого игрока")}, {QStringLiteral("zh"), QStringLiteral("正在等待其他玩家")}, {QStringLiteral("ja"), QStringLiteral("他のプレイヤーを待っています")}, {QStringLiteral("ko"), QStringLiteral("다른 플레이어를 기다리는 중")}, {QStringLiteral("ar"), QStringLiteral("بانتظار لاعب آخر")}, {QStringLiteral("hi"), QStringLiteral("दूसरे खिलाड़ी की प्रतीक्षा")}}},
		{QStringLiteral(" · Spectator"), {{QStringLiteral("es"), QStringLiteral(" · Espectador")}, {QStringLiteral("fr"), QStringLiteral(" · Spectateur")}, {QStringLiteral("de"), QStringLiteral(" · Zuschauer")}, {QStringLiteral("pt"), QStringLiteral(" · Espectador")}, {QStringLiteral("it"), QStringLiteral(" · Spettatore")}, {QStringLiteral("ru"), QStringLiteral(" · Зритель")}, {QStringLiteral("zh"), QStringLiteral(" · 观众")}, {QStringLiteral("ja"), QStringLiteral(" · 観戦者")}, {QStringLiteral("ko"), QStringLiteral(" · 관전자")}, {QStringLiteral("ar"), QStringLiteral(" · مشاهد")}, {QStringLiteral("hi"), QStringLiteral(" · दर्शक")}}},
		{QStringLiteral(" · Drawing"), {{QStringLiteral("es"), QStringLiteral(" · Dibujando")}, {QStringLiteral("fr"), QStringLiteral(" · Dessine")}, {QStringLiteral("de"), QStringLiteral(" · Zeichnet")}, {QStringLiteral("pt"), QStringLiteral(" · Desenhando")}, {QStringLiteral("it"), QStringLiteral(" · Sta disegnando")}, {QStringLiteral("ru"), QStringLiteral(" · Рисует")}, {QStringLiteral("zh"), QStringLiteral(" · 正在绘画")}, {QStringLiteral("ja"), QStringLiteral(" · 描画中")}, {QStringLiteral("ko"), QStringLiteral(" · 그림 그리는 중")}, {QStringLiteral("ar"), QStringLiteral(" · يرسم")}, {QStringLiteral("hi"), QStringLiteral(" · चित्र बना रहा है")}}},
		{QStringLiteral("%1%2%3 (@%4)\nRoom: %5 · Total: %6"), {{QStringLiteral("es"), QStringLiteral("%1%2%3 (@%4)\nSala: %5 · Total: %6")}, {QStringLiteral("fr"), QStringLiteral("%1%2%3 (@%4)\nSalon : %5 · Total : %6")}, {QStringLiteral("de"), QStringLiteral("%1%2%3 (@%4)\nRaum: %5 · Gesamt: %6")}, {QStringLiteral("pt"), QStringLiteral("%1%2%3 (@%4)\nSala: %5 · Total: %6")}, {QStringLiteral("it"), QStringLiteral("%1%2%3 (@%4)\nStanza: %5 · Totale: %6")}, {QStringLiteral("ru"), QStringLiteral("%1%2%3 (@%4)\nКомната: %5 · Всего: %6")}, {QStringLiteral("zh"), QStringLiteral("%1%2%3 (@%4)\n房间：%5 · 总计：%6")}, {QStringLiteral("ja"), QStringLiteral("%1%2%3 (@%4)\nルーム：%5 · 合計：%6")}, {QStringLiteral("ko"), QStringLiteral("%1%2%3 (@%4)\n방: %5 · 합계: %6")}, {QStringLiteral("ar"), QStringLiteral("%1%2%3 (@%4)\nالغرفة: %5 · الإجمالي: %6")}, {QStringLiteral("hi"), QStringLiteral("%1%2%3 (@%4)\nकमरा: %5 · कुल: %6")}}},
		{QStringLiteral("%1 (@%2)\nGuessing: %3 points · %4 · Rank %5\nDrawing: %6 points · %7 · Rank %8\nCombined: %9 points · Rank %10\nWins: %11\nLosses: %12\nWin/Loss ratio: %13"), {{QStringLiteral("es"), QStringLiteral("%1 (@%2)\nAdivinación: %3 puntos · %4 · Rango %5\nDibujo: %6 puntos · %7 · Rango %8\nTotal: %9 puntos · Rango %10\nVictorias: %11\nDerrotas: %12\nRelación victorias/derrotas: %13")}, {QStringLiteral("fr"), QStringLiteral("%1 (@%2)\nDevinettes : %3 points · %4 · Rang %5\nDessin : %6 points · %7 · Rang %8\nTotal : %9 points · Rang %10\nVictoires : %11\nDéfaites : %12\nRatio victoires/défaites : %13")}, {QStringLiteral("de"), QStringLiteral("%1 (@%2)\nRaten: %3 Punkte · %4 · Rang %5\nZeichnen: %6 Punkte · %7 · Rang %8\nGesamt: %9 Punkte · Rang %10\nSiege: %11\nNiederlagen: %12\nSiege-Niederlagen-Verhältnis: %13")}, {QStringLiteral("pt"), QStringLiteral("%1 (@%2)\nAdivinhação: %3 pontos · %4 · Classificação %5\nDesenho: %6 pontos · %7 · Classificação %8\nTotal: %9 pontos · Classificação %10\nVitórias: %11\nDerrotas: %12\nProporção vitórias/derrotas: %13")}, {QStringLiteral("it"), QStringLiteral("%1 (@%2)\nIndovinare: %3 punti · %4 · Posizione %5\nDisegno: %6 punti · %7 · Posizione %8\nTotale: %9 punti · Posizione %10\nVittorie: %11\nSconfitte: %12\nRapporto vittorie/sconfitte: %13")}, {QStringLiteral("ru"), QStringLiteral("%1 (@%2)\nУгадывание: %3 очков · %4 · Ранг %5\nРисование: %6 очков · %7 · Ранг %8\nВсего: %9 очков · Ранг %10\nПобеды: %11\nПоражения: %12\nСоотношение побед и поражений: %13")}, {QStringLiteral("zh"), QStringLiteral("%1 (@%2)\n猜词：%3 分 · %4 · 排名 %5\n绘画：%6 分 · %7 · 排名 %8\n总计：%9 分 · 排名 %10\n胜场：%11\n负场：%12\n胜负比：%13")}, {QStringLiteral("ja"), QStringLiteral("%1 (@%2)\n推測：%3 点 · %4 · 順位 %5\n描画：%6 点 · %7 · 順位 %8\n合計：%9 点 · 順位 %10\n勝利：%11\n敗北：%12\n勝敗比：%13")}, {QStringLiteral("ko"), QStringLiteral("%1 (@%2)\n추측: %3점 · %4 · 순위 %5\n그리기: %6점 · %7 · 순위 %8\n합계: %9점 · 순위 %10\n승리: %11\n패배: %12\n승패 비율: %13")}, {QStringLiteral("ar"), QStringLiteral("%1 (@%2)\nالتخمين: %3 نقطة · %4 · الترتيب %5\nالرسم: %6 نقطة · %7 · الترتيب %8\nالإجمالي: %9 نقطة · الترتيب %10\nالفوز: %11\nالخسارة: %12\nنسبة الفوز/الخسارة: %13")}, {QStringLiteral("hi"), QStringLiteral("%1 (@%2)\nअनुमान: %3 अंक · %4 · रैंक %5\nड्राइंग: %6 अंक · %7 · रैंक %8\nकुल: %9 अंक · रैंक %10\nजीत: %11\nहार: %12\nजीत/हार अनुपात: %13")}}},
		{QStringLiteral("Unranked"), {{QStringLiteral("es"), QStringLiteral("Sin clasificación")}, {QStringLiteral("fr"), QStringLiteral("Non classé")}, {QStringLiteral("de"), QStringLiteral("Ohne Rang")}, {QStringLiteral("pt"), QStringLiteral("Sem classificação")}, {QStringLiteral("it"), QStringLiteral("Non classificato")}, {QStringLiteral("ru"), QStringLiteral("Без рейтинга")}, {QStringLiteral("zh"), QStringLiteral("未排名")}, {QStringLiteral("ja"), QStringLiteral("ランキングなし")}, {QStringLiteral("ko"), QStringLiteral("순위 없음")}, {QStringLiteral("ar"), QStringLiteral("غير مصنف")}, {QStringLiteral("hi"), QStringLiteral("रैंक नहीं")}}},
		{QStringLiteral("Unrated"), {{QStringLiteral("es"), QStringLiteral("Sin puntuación")}, {QStringLiteral("fr"), QStringLiteral("Non évalué")}, {QStringLiteral("de"), QStringLiteral("Nicht bewertet")}, {QStringLiteral("pt"), QStringLiteral("Sem avaliação")}, {QStringLiteral("it"), QStringLiteral("Non valutato")}, {QStringLiteral("ru"), QStringLiteral("Без оценки")}, {QStringLiteral("zh"), QStringLiteral("未评级")}, {QStringLiteral("ja"), QStringLiteral("未評価")}, {QStringLiteral("ko"), QStringLiteral("평가 없음")}, {QStringLiteral("ar"), QStringLiteral("غير مُقيّم")}, {QStringLiteral("hi"), QStringLiteral("रेटिंग नहीं")}}},
		{QStringLiteral("N/A"), {{QStringLiteral("es"), QStringLiteral("N/D")}, {QStringLiteral("fr"), QStringLiteral("N/D")}, {QStringLiteral("de"), QStringLiteral("k. A.")}, {QStringLiteral("pt"), QStringLiteral("N/D")}, {QStringLiteral("it"), QStringLiteral("N/D")}, {QStringLiteral("ru"), QStringLiteral("Н/Д")}, {QStringLiteral("zh"), QStringLiteral("不适用")}, {QStringLiteral("ja"), QStringLiteral("該当なし")}, {QStringLiteral("ko"), QStringLiteral("해당 없음")}, {QStringLiteral("ar"), QStringLiteral("غير متاح")}, {QStringLiteral("hi"), QStringLiteral("लागू नहीं")}}},
		{QStringLiteral("Drawer: receive only"), {{QStringLiteral("es"), QStringLiteral("Dibujante: solo recibir")}, {QStringLiteral("fr"), QStringLiteral("Dessinateur : réception uniquement")}, {QStringLiteral("de"), QStringLiteral("Zeichner: nur empfangen")}, {QStringLiteral("pt"), QStringLiteral("Desenhista: somente receber")}, {QStringLiteral("it"), QStringLiteral("Disegnatore: solo ricezione")}, {QStringLiteral("ru"), QStringLiteral("Рисующий: только приём")}, {QStringLiteral("zh"), QStringLiteral("画手：仅接收")}, {QStringLiteral("ja"), QStringLiteral("描画者：受信のみ")}, {QStringLiteral("ko"), QStringLiteral("그림 담당: 수신만")}, {QStringLiteral("ar"), QStringLiteral("الرسام: استقبال فقط")}, {QStringLiteral("hi"), QStringLiteral("चित्रकार: केवल получатель")}}},
		{QStringLiteral("Guessing"), {{QStringLiteral("es"), QStringLiteral("Adivinación")}, {QStringLiteral("fr"), QStringLiteral("Devinettes")}, {QStringLiteral("de"), QStringLiteral("Raten")}, {QStringLiteral("pt"), QStringLiteral("Adivinhação")}, {QStringLiteral("it"), QStringLiteral("Indovinare")}, {QStringLiteral("ru"), QStringLiteral("Угадывание")}, {QStringLiteral("zh"), QStringLiteral("猜词")}, {QStringLiteral("ja"), QStringLiteral("推測")}, {QStringLiteral("ko"), QStringLiteral("추측")}, {QStringLiteral("ar"), QStringLiteral("التخمين")}, {QStringLiteral("hi"), QStringLiteral("अनुमान")}}},
		{QStringLiteral("Drawing"), {{QStringLiteral("es"), QStringLiteral("Dibujo")}, {QStringLiteral("fr"), QStringLiteral("Dessin")}, {QStringLiteral("de"), QStringLiteral("Zeichnen")}, {QStringLiteral("pt"), QStringLiteral("Desenho")}, {QStringLiteral("it"), QStringLiteral("Disegno")}, {QStringLiteral("ru"), QStringLiteral("Рисование")}, {QStringLiteral("zh"), QStringLiteral("绘画")}, {QStringLiteral("ja"), QStringLiteral("描画")}, {QStringLiteral("ko"), QStringLiteral("그리기")}, {QStringLiteral("ar"), QStringLiteral("الرسم")}, {QStringLiteral("hi"), QStringLiteral("चित्रकारी")}}},
		{QStringLiteral("Combined"), {{QStringLiteral("es"), QStringLiteral("Total")}, {QStringLiteral("fr"), QStringLiteral("Total")}, {QStringLiteral("de"), QStringLiteral("Gesamt")}, {QStringLiteral("pt"), QStringLiteral("Total")}, {QStringLiteral("it"), QStringLiteral("Totale")}, {QStringLiteral("ru"), QStringLiteral("Всего")}, {QStringLiteral("zh"), QStringLiteral("总计")}, {QStringLiteral("ja"), QStringLiteral("合計")}, {QStringLiteral("ko"), QStringLiteral("합계")}, {QStringLiteral("ar"), QStringLiteral("الإجمالي")}, {QStringLiteral("hi"), QStringLiteral("कुल")}}},
		{QStringLiteral("Rank"), {{QStringLiteral("es"), QStringLiteral("Rango")}, {QStringLiteral("fr"), QStringLiteral("Rang")}, {QStringLiteral("de"), QStringLiteral("Rang")}, {QStringLiteral("pt"), QStringLiteral("Classificação")}, {QStringLiteral("it"), QStringLiteral("Posizione")}, {QStringLiteral("ru"), QStringLiteral("Ранг")}, {QStringLiteral("zh"), QStringLiteral("排名")}, {QStringLiteral("ja"), QStringLiteral("順位")}, {QStringLiteral("ko"), QStringLiteral("순위")}, {QStringLiteral("ar"), QStringLiteral("الترتيب")}, {QStringLiteral("hi"), QStringLiteral("रैंक")}}},
		{QStringLiteral("Wins"), {{QStringLiteral("es"), QStringLiteral("Victorias")}, {QStringLiteral("fr"), QStringLiteral("Victoires")}, {QStringLiteral("de"), QStringLiteral("Siege")}, {QStringLiteral("pt"), QStringLiteral("Vitórias")}, {QStringLiteral("it"), QStringLiteral("Vittorie")}, {QStringLiteral("ru"), QStringLiteral("Победы")}, {QStringLiteral("zh"), QStringLiteral("胜场")}, {QStringLiteral("ja"), QStringLiteral("勝利")}, {QStringLiteral("ko"), QStringLiteral("승리")}, {QStringLiteral("ar"), QStringLiteral("الفوز")}, {QStringLiteral("hi"), QStringLiteral("जीत")}}},
		{QStringLiteral("Losses"), {{QStringLiteral("es"), QStringLiteral("Derrotas")}, {QStringLiteral("fr"), QStringLiteral("Défaites")}, {QStringLiteral("de"), QStringLiteral("Niederlagen")}, {QStringLiteral("pt"), QStringLiteral("Derrotas")}, {QStringLiteral("it"), QStringLiteral("Sconfitte")}, {QStringLiteral("ru"), QStringLiteral("Поражения")}, {QStringLiteral("zh"), QStringLiteral("负场")}, {QStringLiteral("ja"), QStringLiteral("敗北")}, {QStringLiteral("ko"), QStringLiteral("패배")}, {QStringLiteral("ar"), QStringLiteral("الخسارة")}, {QStringLiteral("hi"), QStringLiteral("हार")}}},
		{QStringLiteral("Win/Loss ratio"), {{QStringLiteral("es"), QStringLiteral("Relación victorias/derrotas")}, {QStringLiteral("fr"), QStringLiteral("Ratio victoires/défaites")}, {QStringLiteral("de"), QStringLiteral("Siege-Niederlagen-Verhältnis")}, {QStringLiteral("pt"), QStringLiteral("Proporção vitórias/derrotas")}, {QStringLiteral("it"), QStringLiteral("Rapporto vittorie/sconfitte")}, {QStringLiteral("ru"), QStringLiteral("Соотношение побед и поражений")}, {QStringLiteral("zh"), QStringLiteral("胜负比")}, {QStringLiteral("ja"), QStringLiteral("勝敗比")}, {QStringLiteral("ko"), QStringLiteral("승패 비율")}, {QStringLiteral("ar"), QStringLiteral("نسبة الفوز/الخسارة")}, {QStringLiteral("hi"), QStringLiteral("जीत/हार अनुपात")}}},
		{QStringLiteral("Total"), {{QStringLiteral("es"), QStringLiteral("Total")}, {QStringLiteral("fr"), QStringLiteral("Total")}, {QStringLiteral("de"), QStringLiteral("Gesamt")}, {QStringLiteral("pt"), QStringLiteral("Total")}, {QStringLiteral("it"), QStringLiteral("Totale")}, {QStringLiteral("ru"), QStringLiteral("Всего")}, {QStringLiteral("zh"), QStringLiteral("总计")}, {QStringLiteral("ja"), QStringLiteral("合計")}, {QStringLiteral("ko"), QStringLiteral("합계")}, {QStringLiteral("ar"), QStringLiteral("الإجمالي")}, {QStringLiteral("hi"), QStringLiteral("कुल")}}}
	};
	const QString liveText = liveGameTranslations.value(source).value(languageCode);
	if (!liveText.isEmpty())
		return liveText;
	return source;
}

void setLocalizedText(QWidget* widget, const QString& source, const QString& languageCode)
{
	widget->setProperty("gtdSourceText", source);
	widget->setProperty("gtdTextArguments", QVariant());
	const QString text = localizedText(source, languageCode);
	if (auto* label = qobject_cast<QLabel*>(widget))
		label->setText(text);
	else if (auto* button = qobject_cast<QPushButton*>(widget))
		button->setText(text);
	else if (auto* checkBox = qobject_cast<QCheckBox*>(widget))
		checkBox->setText(text);
}

void setLocalizedFormattedText(QWidget* widget, const QString& source, const QStringList& arguments,
	const QString& languageCode)
{
	widget->setProperty("gtdSourceText", source);
	widget->setProperty("gtdTextArguments", arguments);
	QString text = localizedText(source, languageCode);
	for (const QString& argument : arguments)
		text = text.arg(argument);
	if (auto* label = qobject_cast<QLabel*>(widget))
		label->setText(text);
	else if (auto* button = qobject_cast<QPushButton*>(widget))
		button->setText(text);
}

void setLocalizedPlaceholder(QLineEdit* edit, const QString& source, const QString& languageCode)
{
	edit->setProperty("gtdSourcePlaceholder", source);
	edit->setPlaceholderText(localizedText(source, languageCode));
}

void setLocalizedComboItems(QComboBox* combo, const QStringList& sourceItems, const QString& languageCode)
{
	combo->setProperty("gtdSourceItems", sourceItems);
	for (int index = 0; index < sourceItems.size() && index < combo->count(); ++index)
		combo->setItemText(index, localizedText(sourceItems.at(index), languageCode));
}

void setLocalizedTableHeaders(QTableWidget* table, const QStringList& sourceHeaders, const QString& languageCode)
{
	table->setProperty("gtdSourceHeaders", sourceHeaders);
	for (int index = 0; index < sourceHeaders.size() && index < table->columnCount(); ++index)
		table->setHorizontalHeaderItem(index, new QTableWidgetItem(localizedText(sourceHeaders.at(index), languageCode)));
}

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
		const QString latency = languageCode_ == QStringLiteral("es") ? QStringLiteral("latencia")
			: languageCode_ == QStringLiteral("fr") ? QStringLiteral("latence")
			: languageCode_ == QStringLiteral("de") ? QStringLiteral("Latenz")
			: languageCode_ == QStringLiteral("pt") ? QStringLiteral("latência")
			: languageCode_ == QStringLiteral("it") ? QStringLiteral("latenza")
			: languageCode_ == QStringLiteral("ru") ? QStringLiteral("задержка")
			: languageCode_ == QStringLiteral("zh") ? QStringLiteral("延迟")
			: languageCode_ == QStringLiteral("ja") ? QStringLiteral("遅延")
			: languageCode_ == QStringLiteral("ko") ? QStringLiteral("지연")
			: languageCode_ == QStringLiteral("ar") ? QStringLiteral("زمن الاستجابة")
			: languageCode_ == QStringLiteral("hi") ? QStringLiteral("विलंब") : QStringLiteral("latency");
		const QString description = pingMs_ < 0
			? localizedText(QStringLiteral("Ping unavailable"), languageCode_)
			: QStringLiteral("%1 ms · %2 %3").arg(pingMs_).arg(quality(), latency);
		setToolTip(description);
		setAccessibleName(description);
		update();
	}

	void setLanguageCode(const QString& languageCode)
	{
		languageCode_ = languageCode;
		setPing(pingMs_);
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
		const QString description = pingMs_ < 0 ? QStringLiteral("unavailable")
			: pingMs_ <= 60 ? QStringLiteral("excellent")
			: pingMs_ <= 120 ? QStringLiteral("good")
			: pingMs_ <= 200 ? QStringLiteral("fair") : QStringLiteral("weak");
		return localizedText(description, languageCode_);
	}

	int pingMs_ = -1;
	QString languageCode_ = QStringLiteral("en");
};

MainWindow::MainWindow(AppConfig& config, QString masterHost, quint16 masterPort, QWidget* parent)
	: QMainWindow(parent),
	  config_(config),
	  masterHost_(std::move(masterHost)),
	  masterPort_(masterPort)
{
	applyLanguage(config_.stringValue(QStringLiteral("client"), QStringLiteral("language")));
	applyTheme(config_.stringValue(QStringLiteral("client"), QStringLiteral("theme")));
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
	applyLanguage(languageCode_);

	connect(&masterSocket_, &QTcpSocket::connected, this, [this] {
		setLocalizedText(loginConnectionStatusLabel_, QStringLiteral("Connected to master"), languageCode_);
		setLocalizedText(connectionStatusLabel_, QStringLiteral("Connected to master"), languageCode_);
		if (!authToken_.isEmpty()) {
			requestRooms();
			requestLeaderboards();
		}
	});
	connect(&masterSocket_, &QTcpSocket::readyRead, this, [this] { readMaster(); });
	connect(&masterSocket_, &QTcpSocket::disconnected, this, [this] {
		setLocalizedText(connectionStatusLabel_, QStringLiteral("Master disconnected; reconnecting…"), languageCode_);
		setLocalizedText(loginConnectionStatusLabel_, QStringLiteral("Master disconnected; reconnecting…"), languageCode_);
		QTimer::singleShot(2000, this, [this] { connectMaster(); });
	});
	connect(&masterSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		const QStringList arguments{masterHost_, QString::number(masterPort_)};
		setLocalizedFormattedText(connectionStatusLabel_, QStringLiteral("Cannot reach master at %1:%2"), arguments, languageCode_);
		setLocalizedFormattedText(loginConnectionStatusLabel_, QStringLiteral("Cannot reach master at %1:%2"), arguments, languageCode_);
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
			setLocalizedText(connectionStatusLabel_, QStringLiteral("Disconnected from the game server"), languageCode_);
			requestRooms();
		}
		leavingRoom_ = false;
	});
	connect(&gameSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		if (!inRoom_)
			setLocalizedText(connectionStatusLabel_, QStringLiteral("Could not connect to the selected room"), languageCode_);
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

void MainWindow::applyLanguage(const QString& languageCode)
{
	static const QStringList supportedLanguages{
		QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("fr"), QStringLiteral("de"),
		QStringLiteral("pt"), QStringLiteral("it"), QStringLiteral("ru"), QStringLiteral("zh"),
		QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("ar"), QStringLiteral("hi")};
	languageCode_ = supportedLanguages.contains(languageCode) ? languageCode : QStringLiteral("en");
	QApplication::setLayoutDirection(languageCode_ == QStringLiteral("ar")
		? Qt::RightToLeft : Qt::LeftToRight);
	const QList<QWidget*> pages{loginPage_, lobbyPage_, gamePage_};
	for (QWidget* page : pages) {
		if (!page)
			continue;
		for (QWidget* widget : page->findChildren<QWidget*>()) {
			const QString source = widget->property("gtdSourceText").toString();
			if (!source.isEmpty()) {
				QString text = localizedText(source, languageCode_);
				for (const QString& argument : widget->property("gtdTextArguments").toStringList())
					text = text.arg(argument);
				if (auto* label = qobject_cast<QLabel*>(widget))
					label->setText(text);
				else if (auto* button = qobject_cast<QPushButton*>(widget))
					button->setText(text);
				else if (auto* checkBox = qobject_cast<QCheckBox*>(widget))
					checkBox->setText(text);
			}
			const QString placeholder = widget->property("gtdSourcePlaceholder").toString();
			if (!placeholder.isEmpty()) {
				if (auto* edit = qobject_cast<QLineEdit*>(widget))
					edit->setPlaceholderText(localizedText(placeholder, languageCode_));
			}
			const QString tooltip = widget->property("gtdSourceToolTip").toString();
			if (!tooltip.isEmpty())
				widget->setToolTip(localizedText(tooltip, languageCode_));
			const QString accessibleName = widget->property("gtdSourceAccessibleName").toString();
			if (!accessibleName.isEmpty())
				widget->setAccessibleName(localizedText(accessibleName, languageCode_));
			if (auto* latency = dynamic_cast<LatencyIndicator*>(widget))
				latency->setLanguageCode(languageCode_);
			const QStringList comboItems = widget->property("gtdSourceItems").toStringList();
			if (auto* combo = qobject_cast<QComboBox*>(widget)) {
				for (int index = 0; index < comboItems.size() && index < combo->count(); ++index)
					combo->setItemText(index, localizedText(comboItems.at(index), languageCode_));
			}
			const QStringList headers = widget->property("gtdSourceHeaders").toStringList();
			if (auto* table = qobject_cast<QTableWidget*>(widget)) {
				for (int index = 0; index < headers.size() && index < table->columnCount(); ++index) {
					if (table->horizontalHeaderItem(index))
						table->horizontalHeaderItem(index)->setText(localizedText(headers.at(index), languageCode_));
				}
			}
		}
		for (QTableWidget* table : page->findChildren<QTableWidget*>()) {
			for (int row = 0; row < table->rowCount(); ++row) {
				for (int column = 0; column < table->columnCount(); ++column) {
					QTableWidgetItem* item = table->item(row, column);
					if (!item)
						continue;
					const QString itemSource = item->data(Qt::UserRole).toString();
					const QStringList arguments = item->data(Qt::UserRole + 1).toStringList();
					if (itemSource.isEmpty() || arguments.isEmpty())
						continue;
					QString text = localizedText(itemSource, languageCode_);
					for (const QString& argument : arguments)
						text = text.arg(argument);
					item->setText(text);
				}
			}
		}
		for (QTabWidget* tabWidget : page->findChildren<QTabWidget*>()) {
			for (int index = 0; index < tabWidget->count(); ++index) {
				QWidget* tab = tabWidget->widget(index);
				const QString source = tab->property("gtdTabSourceText").toString();
				if (!source.isEmpty())
					tabWidget->setTabText(index, localizedText(source, languageCode_));
			}
		}
	}
	if (inRoom_)
		updateRoundControls();
	if (countryCombo_ && countryCombo_->count() > 0)
		countryCombo_->setItemText(0, languageCode_ == QStringLiteral("es")
			? QStringLiteral("Elegir país…") : languageCode_ == QStringLiteral("fr")
			? QStringLiteral("Choisir un pays…") : languageCode_ == QStringLiteral("de")
			? QStringLiteral("Land auswählen…") : languageCode_ == QStringLiteral("pt")
			? QStringLiteral("Escolher país…") : languageCode_ == QStringLiteral("it")
			? QStringLiteral("Scegli paese…") : languageCode_ == QStringLiteral("ru")
			? QStringLiteral("Выберите страну…") : languageCode_ == QStringLiteral("zh")
			? QStringLiteral("选择国家…") : languageCode_ == QStringLiteral("ja")
			? QStringLiteral("国を選択…") : languageCode_ == QStringLiteral("ko")
			? QStringLiteral("국가 선택…") : languageCode_ == QStringLiteral("ar")
			? QStringLiteral("اختر الدولة…") : languageCode_ == QStringLiteral("hi")
			? QStringLiteral("देश चुनें…") : QStringLiteral("Choose country…"));
	if (inRoom_ && !roomRoster_.isEmpty())
		updateRoster(roomRoster_, {});
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
	setLocalizedPlaceholder(usernameEdit_, QStringLiteral("3-24 letters, numbers, or underscores"), languageCode_);
	passwordEdit_ = new QLineEdit(page);
	passwordEdit_->setEchoMode(QLineEdit::Password);
	passwordEdit_->setMaxLength(128);
	displayNameEdit_ = new QLineEdit(page);
	displayNameEdit_->setMaxLength(32);
	setLocalizedPlaceholder(displayNameEdit_, QStringLiteral("Shown to other players"), languageCode_);
	displayNameEdit_->hide();
	for (const auto& field : {qMakePair(QStringLiteral("Username"), usernameEdit_),
			qMakePair(QStringLiteral("Password"), passwordEdit_),
			qMakePair(QStringLiteral("Display name"), displayNameEdit_)}) {
		auto* label = new QLabel(page);
		setLocalizedText(label, field.first, languageCode_);
		form->addRow(label, field.second);
	}
	outer->addLayout(form);
	createAccountCheck_ = new QCheckBox(QStringLiteral("Create a new account"), page);
	setLocalizedText(createAccountCheck_, QStringLiteral("Create a new account"), languageCode_);
	outer->addWidget(createAccountCheck_);
	connect(createAccountCheck_, &QCheckBox::toggled, this, [this](bool checked) {
		displayNameEdit_->setVisible(checked);
	});
	auto* continueButton = new QPushButton(QStringLiteral("Continue"), page);
	setLocalizedText(continueButton, QStringLiteral("Continue"), languageCode_);
	continueButton->setDefault(true);
	outer->addWidget(continueButton);
	loginStatusLabel_ = new QLabel(page);
	loginStatusLabel_->setWordWrap(true);
	outer->addWidget(loginStatusLabel_);
	loginConnectionStatusLabel_ = new QLabel(QStringLiteral("Connecting to master…"), page);
	setLocalizedText(loginConnectionStatusLabel_, QStringLiteral("Connecting to master…"), languageCode_);
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
	setLocalizedText(heading, QStringLiteral("Lobby"), languageCode_);
	QFont font = heading->font();
	font.setPointSize(20);
	font.setBold(true);
	heading->setFont(font);
	layout->addWidget(heading);
	auto* topRow = new QGridLayout;
	auto* accountLabel = new QLabel(page);
	setLocalizedFormattedText(accountLabel, QStringLiteral("Signed in as %1"), {username_}, languageCode_);
	accountLabel->setObjectName(QStringLiteral("accountLabel"));
	topRow->addWidget(accountLabel, 0, 0);
	connectionStatusLabel_ = new QLabel(QStringLiteral("Connecting to master…"), page);
	setLocalizedText(connectionStatusLabel_, QStringLiteral("Connecting to master…"), languageCode_);
	connectionStatusLabel_->setObjectName(QStringLiteral("lobbyStatusLabel"));
	topRow->addWidget(connectionStatusLabel_, 0, 1);
	countryCombo_ = new QComboBox(page);
	countryCombo_->setObjectName(QStringLiteral("countryCombo"));
	countryCombo_->setIconSize(QSize(24, 16));
	countryCombo_->addItem(QStringLiteral("Choose country…"), QString());
	const QStringList countryCodes = QStringLiteral(
		"AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT "
		"BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH "
		"ER ES ET FI FJ FK FM FO FR GA GB GB-ENG GB-NIR GB-SCT GB-WLS GD GE GF GG GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT "
		"HU ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS "
		"LT LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI "
		"NL NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG "
		"SH SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG "
		"UM US UY UZ VA VC VE VG VI VN VU WF WS XK YE YT ZA ZM ZW").split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (const QString& countryCode : countryCodes)
		countryCombo_->addItem(countryFlagIcon(countryCode), countryCode, countryCode);
	auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), page);
	setLocalizedText(refreshButton, QStringLiteral("Refresh"), languageCode_);
	topRow->addWidget(refreshButton, 0, 2);
	layout->addLayout(topRow);
	auto* tabs = new QTabWidget(page);
	auto* roomsPage = new QWidget(tabs);
	auto* roomsLayout = new QVBoxLayout(roomsPage);
	roomsTable_ = new QTableWidget(0, 5, page);
	setLocalizedTableHeaders(roomsTable_, {QStringLiteral("Room"), QStringLiteral("Players"),
		QStringLiteral("Round timer"), QStringLiteral("Ping"), QStringLiteral("Server")}, languageCode_);
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
	setLocalizedText(hostInstructions, QStringLiteral("Rooms are hosted by dedicated game-server processes."), languageCode_);
	bottomRow->addWidget(hostInstructions, 0, 0);
	joinRoomButton_ = new QPushButton(QStringLiteral("Join selected room"), page);
	setLocalizedText(joinRoomButton_, QStringLiteral("Join selected room"), languageCode_);
	joinRoomButton_->setEnabled(false);
	joinRoleCombo_ = new QComboBox(page);
	joinRoleCombo_->addItem(QStringLiteral("Join as player"), false);
	joinRoleCombo_->addItem(QStringLiteral("Join as spectator"), true);
	setLocalizedComboItems(joinRoleCombo_, {QStringLiteral("Join as player"), QStringLiteral("Join as spectator")}, languageCode_);
	bottomRow->addWidget(joinRoleCombo_, 0, 1);
	bottomRow->addWidget(joinRoomButton_, 0, 2);
	auto* signOutButton = new QPushButton(QStringLiteral("Sign out"), page);
	setLocalizedText(signOutButton, QStringLiteral("Sign out"), languageCode_);
	bottomRow->addWidget(signOutButton, 0, 3);
	roomsLayout->addLayout(bottomRow);
	tabs->addTab(roomsPage, QStringLiteral("Rooms"));
	roomsPage->setProperty("gtdTabSourceText", QStringLiteral("Rooms"));
	auto* leaderboardPage = new QWidget(tabs);
	auto* leaderboardLayout = new QVBoxLayout(leaderboardPage);
	auto* leaderboardTabs = new QTabWidget(leaderboardPage);
	guessingLeaderboardTable_ = new QTableWidget(0, 4, leaderboardTabs);
	setLocalizedTableHeaders(guessingLeaderboardTable_, {QStringLiteral("Rank"), QStringLiteral("Player"),
		QStringLiteral("Country"), QStringLiteral("Guessing points")}, languageCode_);
	drawingLeaderboardTable_ = new QTableWidget(0, 4, leaderboardTabs);
	setLocalizedTableHeaders(drawingLeaderboardTable_, {QStringLiteral("Rank"), QStringLiteral("Player"),
		QStringLiteral("Country"), QStringLiteral("Drawing points")}, languageCode_);
	for (QTableWidget* table : {guessingLeaderboardTable_, drawingLeaderboardTable_}) {
		table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	}
	leaderboardTabs->addTab(guessingLeaderboardTable_, QStringLiteral("Guessing"));
	guessingLeaderboardTable_->setProperty("gtdTabSourceText", QStringLiteral("Guessing"));
	leaderboardTabs->addTab(drawingLeaderboardTable_, QStringLiteral("Drawing"));
	drawingLeaderboardTable_->setProperty("gtdTabSourceText", QStringLiteral("Drawing"));
	leaderboardLayout->addWidget(leaderboardTabs);
	tabs->addTab(leaderboardPage, QStringLiteral("Leaderboards"));
	leaderboardPage->setProperty("gtdTabSourceText", QStringLiteral("Leaderboards"));
	auto* settingsPage = new QWidget(tabs);
	auto* settingsLayout = new QVBoxLayout(settingsPage);
	auto* settingsTabs = new QTabWidget(settingsPage);
	auto* profilePage = new QWidget(settingsTabs);
	auto* profileLayout = new QFormLayout(profilePage);
	nicknameEdit_ = new QLineEdit(profilePage);
	nicknameEdit_->setMaxLength(32);
	auto* nicknameLabel = new QLabel(profilePage);
	setLocalizedText(nicknameLabel, QStringLiteral("Nickname"), languageCode_);
	profileLayout->addRow(nicknameLabel, nicknameEdit_);
	auto* countryLabel = new QLabel(profilePage);
	setLocalizedText(countryLabel, QStringLiteral("Country / flag"), languageCode_);
	profileLayout->addRow(countryLabel, countryCombo_);
	currentPasswordEdit_ = new QLineEdit(profilePage);
	currentPasswordEdit_->setEchoMode(QLineEdit::Password);
	currentPasswordEdit_->setMaxLength(128);
	currentPasswordEdit_->setProperty("gtdSourcePlaceholder", QStringLiteral("Required to change password"));
	currentPasswordEdit_->setPlaceholderText(localizedText(QStringLiteral("Required to change password"), languageCode_));
	auto* currentPasswordLabel = new QLabel(profilePage);
	setLocalizedText(currentPasswordLabel, QStringLiteral("Current password"), languageCode_);
	profileLayout->addRow(currentPasswordLabel, currentPasswordEdit_);
	newPasswordEdit_ = new QLineEdit(profilePage);
	newPasswordEdit_->setEchoMode(QLineEdit::Password);
	newPasswordEdit_->setMaxLength(128);
	newPasswordEdit_->setProperty("gtdSourcePlaceholder", QStringLiteral("Leave blank to keep current password"));
	newPasswordEdit_->setPlaceholderText(localizedText(QStringLiteral("Leave blank to keep current password"), languageCode_));
	auto* newPasswordLabel = new QLabel(profilePage);
	setLocalizedText(newPasswordLabel, QStringLiteral("New password"), languageCode_);
	profileLayout->addRow(newPasswordLabel, newPasswordEdit_);
	auto* saveProfileButton = new QPushButton(QStringLiteral("Save profile"), profilePage);
	setLocalizedText(saveProfileButton, QStringLiteral("Save profile"), languageCode_);
	profileStatusLabel_ = new QLabel(profilePage);
	profileStatusLabel_->setWordWrap(true);
	auto* profileActions = new QHBoxLayout;
	profileActions->addWidget(saveProfileButton);
	profileActions->addWidget(profileStatusLabel_, 1);
	profileLayout->addRow(profileActions);
	profilePage->setProperty("gtdTabSourceText", QStringLiteral("Profile settings"));
	settingsTabs->addTab(profilePage, localizedText(QStringLiteral("Profile settings"), languageCode_));
	auto* appSettingsPage = new QWidget(settingsTabs);
	auto* appSettingsLayout = new QFormLayout(appSettingsPage);
	languageCombo_ = new QComboBox(appSettingsPage);
	const QList<QPair<QString, QString>> languages{
		{QStringLiteral("English"), QStringLiteral("en")},
		{QStringLiteral("Español"), QStringLiteral("es")},
		{QStringLiteral("Français"), QStringLiteral("fr")},
		{QStringLiteral("Deutsch"), QStringLiteral("de")},
		{QStringLiteral("Português"), QStringLiteral("pt")},
		{QStringLiteral("Italiano"), QStringLiteral("it")},
		{QStringLiteral("Русский"), QStringLiteral("ru")},
		{QStringLiteral("中文（简体）"), QStringLiteral("zh")},
		{QStringLiteral("日本語"), QStringLiteral("ja")},
		{QStringLiteral("한국어"), QStringLiteral("ko")},
		{QStringLiteral("العربية"), QStringLiteral("ar")},
		{QStringLiteral("हिन्दी"), QStringLiteral("hi")}};
	for (const auto& language : languages)
		languageCombo_->addItem(language.first, language.second);
	const int languageIndex = languageCombo_->findData(languageCode_);
	languageCombo_->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
	auto* languageLabel = new QLabel(appSettingsPage);
	setLocalizedText(languageLabel, QStringLiteral("Language"), languageCode_);
	appSettingsLayout->addRow(languageLabel, languageCombo_);
	themeCombo_ = new QComboBox(appSettingsPage);
	const QList<QPair<QString, QString>> themes{
		{QStringLiteral("Light"), QStringLiteral("light")},
		{QStringLiteral("Dark"), QStringLiteral("dark")},
		{QStringLiteral("System"), QStringLiteral("system")},
		{QStringLiteral("Ocean"), QStringLiteral("ocean")},
		{QStringLiteral("Forest"), QStringLiteral("forest")},
		{QStringLiteral("Sunset"), QStringLiteral("sunset")},
		{QStringLiteral("Purple"), QStringLiteral("purple")},
		{QStringLiteral("Solarized"), QStringLiteral("solarized")},
		{QStringLiteral("High Contrast"), QStringLiteral("high-contrast")},
		{QStringLiteral("Rose"), QStringLiteral("rose")},
		{QStringLiteral("Midnight"), QStringLiteral("midnight")},
		{QStringLiteral("Nord"), QStringLiteral("nord")},
		{QStringLiteral("Dracula"), QStringLiteral("dracula")},
		{QStringLiteral("Monokai"), QStringLiteral("monokai")}};
	QStringList themeNames;
	for (const auto& themeOption : themes) {
		themeCombo_->addItem(themeOption.first, themeOption.second);
		themeNames.append(themeOption.first);
	}
	themeCombo_->setProperty("gtdSourceItems", themeNames);
	const int themeIndex = themeCombo_->findData(config_.stringValue(QStringLiteral("client"), QStringLiteral("theme")));
	themeCombo_->setCurrentIndex(themeIndex >= 0 ? themeIndex : themeCombo_->findData(QStringLiteral("system")));
	auto* themeLabel = new QLabel(appSettingsPage);
	setLocalizedText(themeLabel, QStringLiteral("Theme"), languageCode_);
	appSettingsLayout->addRow(themeLabel, themeCombo_);
	settingsStatusLabel_ = new QLabel(appSettingsPage);
	settingsStatusLabel_->setWordWrap(true);
	appSettingsLayout->addRow(settingsStatusLabel_);
	appSettingsPage->setProperty("gtdTabSourceText", QStringLiteral("App settings"));
	settingsTabs->addTab(appSettingsPage, localizedText(QStringLiteral("App settings"), languageCode_));
	settingsLayout->addWidget(settingsTabs);
	settingsPage->setProperty("gtdTabSourceText", QStringLiteral("Settings"));
	tabs->addTab(settingsPage, localizedText(QStringLiteral("Settings"), languageCode_));
	layout->addWidget(tabs, 1);
	connect(refreshButton, &QPushButton::clicked, this, [this] {
		requestRooms();
		requestLeaderboards();
	});
	connect(saveProfileButton, &QPushButton::clicked, this, [this] { saveProfile(); });
	connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
		const QString languageCode = languageCombo_->itemData(index).toString();
		QString error;
		if (!config_.setStringValue(QStringLiteral("client"), QStringLiteral("language"), languageCode, &error)) {
			const QSignalBlocker blocker(languageCombo_);
			languageCombo_->setCurrentIndex(languageCombo_->findData(languageCode_));
			setLocalizedFormattedText(settingsStatusLabel_, QStringLiteral("Could not save language setting: %1"), {error}, languageCode_);
			return;
		}
		settingsStatusLabel_->clear();
		applyLanguage(languageCode);
	});
	connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
		const QString theme = themeCombo_->itemData(index).toString();
		QString error;
		if (!config_.setStringValue(QStringLiteral("client"), QStringLiteral("theme"), theme, &error)) {
			const QSignalBlocker blocker(themeCombo_);
			themeCombo_->setCurrentIndex(themeCombo_->findData(config_.stringValue(
				QStringLiteral("client"), QStringLiteral("theme"))));
			setLocalizedFormattedText(settingsStatusLabel_, QStringLiteral("Could not save theme setting: %1"),
				{error}, languageCode_);
			return;
		}
		settingsStatusLabel_->clear();
		applyTheme(theme);
	});
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

void MainWindow::applyTheme(const QString& theme)
{
	const QPalette systemPalette = QApplication::style()->standardPalette();
	if (theme == QStringLiteral("system") || theme == QStringLiteral("other")) {
		QApplication::setPalette(systemPalette);
		return;
	}

	QPalette palette = systemPalette;
	auto setColors = [&palette](const QString& window, const QString& surface, const QString& base,
								const QString& text, const QString& accent, const QString& accentText,
								const QString& button, const QString& disabledText) {
		palette.setColor(QPalette::Window, QColor(window));
		palette.setColor(QPalette::WindowText, QColor(text));
		palette.setColor(QPalette::Base, QColor(base));
		palette.setColor(QPalette::AlternateBase, QColor(surface));
		palette.setColor(QPalette::ToolTipBase, QColor(surface));
		palette.setColor(QPalette::ToolTipText, QColor(text));
		palette.setColor(QPalette::Text, QColor(text));
		palette.setColor(QPalette::PlaceholderText, QColor(disabledText));
		palette.setColor(QPalette::Button, QColor(button));
		palette.setColor(QPalette::ButtonText, QColor(text));
		palette.setColor(QPalette::BrightText, QColor(accent));
		palette.setColor(QPalette::Link, QColor(accent));
		palette.setColor(QPalette::Highlight, QColor(accent));
		palette.setColor(QPalette::HighlightedText, QColor(accentText));
		palette.setColor(QPalette::Disabled, QPalette::Text, QColor(disabledText));
		palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(disabledText));
	};
	if (theme == QStringLiteral("light")) {
		setColors(QStringLiteral("#f5f6f8"), QStringLiteral("#f1f3f4"), QStringLiteral("#ffffff"),
			QStringLiteral("#202124"), QStringLiteral("#1a73e8"), QStringLiteral("#ffffff"),
			QStringLiteral("#e8eaed"), QStringLiteral("#80868b"));
	} else if (theme == QStringLiteral("dark")) {
		setColors(QStringLiteral("#202124"), QStringLiteral("#292a2d"), QStringLiteral("#17181a"),
			QStringLiteral("#e8eaed"), QStringLiteral("#8ab4f8"), QStringLiteral("#202124"),
			QStringLiteral("#303134"), QStringLiteral("#9aa0a6"));
	} else if (theme == QStringLiteral("ocean")) {
		setColors(QStringLiteral("#0b1f2a"), QStringLiteral("#173747"), QStringLiteral("#102c3a"),
			QStringLiteral("#e3f6fc"), QStringLiteral("#35c4d0"), QStringLiteral("#062027"),
			QStringLiteral("#174454"), QStringLiteral("#87a9b5"));
	} else if (theme == QStringLiteral("forest")) {
		setColors(QStringLiteral("#16251b"), QStringLiteral("#263c2d"), QStringLiteral("#1d3023"),
			QStringLiteral("#e5f2e8"), QStringLiteral("#8ccf83"), QStringLiteral("#15251a"),
			QStringLiteral("#304b37"), QStringLiteral("#9caf9e"));
	} else if (theme == QStringLiteral("sunset")) {
		setColors(QStringLiteral("#2a1b2d"), QStringLiteral("#47303c"), QStringLiteral("#352333"),
			QStringLiteral("#fff0e6"), QStringLiteral("#ff9a62"), QStringLiteral("#30180c"),
			QStringLiteral("#513440"), QStringLiteral("#baa5ad"));
	} else if (theme == QStringLiteral("purple")) {
		setColors(QStringLiteral("#211a2e"), QStringLiteral("#382b4b"), QStringLiteral("#2b203b"),
			QStringLiteral("#f1e9ff"), QStringLiteral("#c4a1ff"), QStringLiteral("#241333"),
			QStringLiteral("#403052"), QStringLiteral("#ab9dbb"));
	} else if (theme == QStringLiteral("solarized")) {
		setColors(QStringLiteral("#002b36"), QStringLiteral("#073642"), QStringLiteral("#00212b"),
			QStringLiteral("#839496"), QStringLiteral("#268bd2"), QStringLiteral("#fdf6e3"),
			QStringLiteral("#073642"), QStringLiteral("#586e75"));
	} else if (theme == QStringLiteral("high-contrast")) {
		setColors(QStringLiteral("#000000"), QStringLiteral("#000000"), QStringLiteral("#000000"),
			QStringLiteral("#ffffff"), QStringLiteral("#ffff00"), QStringLiteral("#000000"),
			QStringLiteral("#000000"), QStringLiteral("#d0d0d0"));
	} else if (theme == QStringLiteral("rose")) {
		setColors(QStringLiteral("#fff5f7"), QStringLiteral("#fce4ec"), QStringLiteral("#ffffff"),
			QStringLiteral("#44212a"), QStringLiteral("#d84a7b"), QStringLiteral("#ffffff"),
			QStringLiteral("#f8d7e2"), QStringLiteral("#9a7c85"));
	} else if (theme == QStringLiteral("midnight")) {
		setColors(QStringLiteral("#101828"), QStringLiteral("#202d46"), QStringLiteral("#141e32"),
			QStringLiteral("#e7edff"), QStringLiteral("#65d6ff"), QStringLiteral("#06151e"),
			QStringLiteral("#253653"), QStringLiteral("#98a6c4"));
	} else if (theme == QStringLiteral("nord")) {
		setColors(QStringLiteral("#2e3440"), QStringLiteral("#434c5e"), QStringLiteral("#3b4252"),
			QStringLiteral("#eceff4"), QStringLiteral("#88c0d0"), QStringLiteral("#2e3440"),
			QStringLiteral("#4c566a"), QStringLiteral("#aab2bf"));
	} else if (theme == QStringLiteral("dracula")) {
		setColors(QStringLiteral("#282a36"), QStringLiteral("#44475a"), QStringLiteral("#343746"),
			QStringLiteral("#f8f8f2"), QStringLiteral("#bd93f9"), QStringLiteral("#282a36"),
			QStringLiteral("#44475a"), QStringLiteral("#a7a9b5"));
	} else if (theme == QStringLiteral("monokai")) {
		setColors(QStringLiteral("#272822"), QStringLiteral("#3e3d32"), QStringLiteral("#2e2d27"),
			QStringLiteral("#f8f8f2"), QStringLiteral("#a6e22e"), QStringLiteral("#1c1d16"),
			QStringLiteral("#49483e"), QStringLiteral("#a6a69f"));
	} else {
		QApplication::setPalette(systemPalette);
		return;
	}
	QApplication::setPalette(palette);
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
	auto* activePingLabel = new QLabel(QStringLiteral("Ping"), activePingWidget);
	setLocalizedText(activePingLabel, QStringLiteral("Ping"), languageCode_);
	activePingLayout->addWidget(activePingLabel);
	activeLatencyIndicator_ = new LatencyIndicator(activePingWidget);
	activePingLayout->addWidget(activeLatencyIndicator_);
	queueStatusLabel_ = new QLabel(page);
	queueStatusLabel_->hide();
	queueProgressBar_ = new QProgressBar(page);
	queueProgressBar_->setProperty("gtdSourceAccessibleName", QStringLiteral("Queue progress"));
	queueProgressBar_->setAccessibleName(localizedText(QStringLiteral("Queue progress"), languageCode_));
	queueProgressBar_->setToolTip(localizedText(QStringLiteral("Progress toward the front of the player queue"), languageCode_));
	queueProgressBar_->setProperty("gtdSourceToolTip", QStringLiteral("Progress toward the front of the player queue"));
	queueProgressBar_->hide();
	roundStatusLabel_ = new QLabel(QStringLiteral("Waiting for players"), page);
	setLocalizedText(roundStatusLabel_, QStringLiteral("Waiting for players"), languageCode_);
	wordLabel_ = new QLabel(QStringLiteral("Your word: —"), page);
	setLocalizedText(wordLabel_, QStringLiteral("Your word: —"), languageCode_);
	timerLabel_ = new QLabel(QStringLiteral("—:—"), page);
	timerLabel_->setAlignment(Qt::AlignCenter);
	QFont timerFont = timerLabel_->font();
	timerFont.setPointSize(18);
	timerFont.setBold(true);
	timerLabel_->setFont(timerFont);
	auto* leaveButton = new QPushButton(QStringLiteral("Leave room"), page);
	setLocalizedText(leaveButton, QStringLiteral("Leave room"), languageCode_);
	promoteToPlayerButton_ = new QPushButton(QStringLiteral("Join as Player"), page);
	setLocalizedText(promoteToPlayerButton_, QStringLiteral("Join as Player"), languageCode_);
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
	QStringList toolLabels;
	for (const auto& tool : tools)
		toolLabels.push_back(tool.first);
	setLocalizedComboItems(toolCombo_, toolLabels, languageCode_);
	toolbar->addWidget(toolCombo_);
	colorButton_ = new QPushButton(QStringLiteral("Color"), page);
	setLocalizedText(colorButton_, QStringLiteral("Color"), languageCode_);
	colorButton_->setStyleSheet(QStringLiteral("background-color: #202124; color: white"));
	toolbar->addWidget(colorButton_);
	auto* widthLabel = new QLabel(QStringLiteral("Brush / eraser size"), page);
	setLocalizedText(widthLabel, QStringLiteral("Brush / eraser size"), languageCode_);
	widthLabel->setToolTip(localizedText(QStringLiteral("The same size controls pen width and the stroke eraser radius."), languageCode_));
	widthLabel->setProperty("gtdSourceToolTip", QStringLiteral("The same size controls pen width and the stroke eraser radius."));
	toolbar->addWidget(widthLabel);
	strokeWidthSpin_ = new QSpinBox(page);
	strokeWidthSpin_->setRange(1, 32);
	strokeWidthSpin_->setValue(4);
	toolbar->addWidget(strokeWidthSpin_);
	auto* textSizeLabel = new QLabel(QStringLiteral("Text size"), page);
	setLocalizedText(textSizeLabel, QStringLiteral("Text size"), languageCode_);
	toolbar->addWidget(textSizeLabel);
	fontSizeSpin_ = new QSpinBox(page);
	fontSizeSpin_->setRange(8, 96);
	fontSizeSpin_->setValue(24);
	toolbar->addWidget(fontSizeSpin_);
	filledCheck_ = new QCheckBox(QStringLiteral("Fill shapes"), page);
	setLocalizedText(filledCheck_, QStringLiteral("Fill shapes"), languageCode_);
	toolbar->addWidget(filledCheck_);
	toolCombo_->setCurrentIndex(1);
	clearLayerButton_ = new QPushButton(QStringLiteral("Clear Selected Layer"), page);
	setLocalizedText(clearLayerButton_, QStringLiteral("Clear Selected Layer"), languageCode_);
	toolbar->addWidget(clearLayerButton_);
	clearCanvasButton_ = new QPushButton(QStringLiteral("Clear All Layers"), page);
	setLocalizedText(clearCanvasButton_, QStringLiteral("Clear All Layers"), languageCode_);
	toolbar->addWidget(clearCanvasButton_);
	toolbar->addStretch();
	layout->addLayout(toolbar);

	auto* body = new QHBoxLayout;
	canvas_ = new DrawingCanvas(page);
	canvas_->setTool(DrawingCanvas::Tool::Pen);
	body->addWidget(canvas_, 1);
	auto* sidePanel = new QVBoxLayout;
	auto* layersLabel = new QLabel(QStringLiteral("Layers · bottom to top"), page);
	setLocalizedText(layersLabel, QStringLiteral("Layers · bottom to top"), languageCode_);
	sidePanel->addWidget(layersLabel);
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
	setLocalizedText(addLayerButton_, QStringLiteral("Add"), languageCode_);
	setLocalizedText(deleteLayerButton_, QStringLiteral("Delete"), languageCode_);
	setLocalizedText(renameLayerButton_, QStringLiteral("Rename"), languageCode_);
	setLocalizedText(visibilityLayerButton_, QStringLiteral("Hide"), languageCode_);
	setLocalizedText(lockLayerButton_, QStringLiteral("Lock"), languageCode_);
	setLocalizedText(layerUpButton_, QStringLiteral("Move up"), languageCode_);
	setLocalizedText(layerDownButton_, QStringLiteral("Move down"), languageCode_);
	layerButtons->addWidget(addLayerButton_, 0, 0);
	layerButtons->addWidget(deleteLayerButton_, 0, 1);
	layerButtons->addWidget(renameLayerButton_, 1, 0);
	layerButtons->addWidget(visibilityLayerButton_, 1, 1);
	layerButtons->addWidget(lockLayerButton_, 2, 0);
	layerButtons->addWidget(layerUpButton_, 2, 1);
	layerButtons->addWidget(layerDownButton_, 3, 0, 1, 2);
	sidePanel->addLayout(layerButtons);
	auto* rosterLabel = new QLabel(QStringLiteral("Players and spectators"), page);
	setLocalizedText(rosterLabel, QStringLiteral("Players and spectators"), languageCode_);
	sidePanel->addWidget(rosterLabel);
	rosterList_ = new QListWidget(page);
	rosterList_->setIconSize(QSize(24, 16));
	rosterList_->setMinimumHeight(90);
	rosterList_->setMaximumHeight(140);
	sidePanel->addWidget(rosterList_);
	auto* chatLabel = new QLabel(QStringLiteral("Room chat and guesses"), page);
	setLocalizedText(chatLabel, QStringLiteral("Room chat and guesses"), languageCode_);
	sidePanel->addWidget(chatLabel);
	chatList_ = new QListWidget(page);
	chatList_->setMinimumHeight(100);
	sidePanel->addWidget(chatList_, 1);
	auto* guessRow = new QGridLayout;
	guessEdit_ = new QLineEdit(page);
	setLocalizedPlaceholder(guessEdit_, QStringLiteral("Type your guess"), languageCode_);
	guessEdit_->setMaxLength(100);
	sendGuessButton_ = new QPushButton(QStringLiteral("Guess"), page);
	setLocalizedText(sendGuessButton_, QStringLiteral("Guess"), languageCode_);
	guessRow->addWidget(guessEdit_, 0, 0);
	guessRow->addWidget(sendGuessButton_, 0, 1);
	sidePanel->addLayout(guessRow);
	auto* roomChatRow = new QHBoxLayout;
	roomChatEdit_ = new QLineEdit(page);
	setLocalizedPlaceholder(roomChatEdit_, QStringLiteral("Send a room message"), languageCode_);
	roomChatEdit_->setMaxLength(300);
	sendChatButton_ = new QPushButton(QStringLiteral("Send"), page);
	setLocalizedText(sendChatButton_, QStringLiteral("Send"), languageCode_);
	roomChatRow->addWidget(roomChatEdit_, 1);
	roomChatRow->addWidget(sendChatButton_);
	sidePanel->addLayout(roomChatRow);
	auto* voiceRow = new QHBoxLayout;
	voiceButton_ = new QPushButton(QStringLiteral("Enable microphone"), page);
	setLocalizedText(voiceButton_, QStringLiteral("Enable microphone"), languageCode_);
	voiceButton_->setCheckable(true);
	voiceStatusLabel_ = new QLabel(QStringLiteral("Microphone off"), page);
	setLocalizedText(voiceStatusLabel_, QStringLiteral("Microphone off"), languageCode_);
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
		const QString name = QInputDialog::getText(this, localizedText(QStringLiteral("Add layer"), languageCode_),
			localizedText(QStringLiteral("Layer name"), languageCode_), QLineEdit::Normal,
			localizedText(QStringLiteral("New layer"), languageCode_), &accepted);
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
		const QString name = QInputDialog::getText(this, localizedText(QStringLiteral("Rename layer"), languageCode_),
			localizedText(QStringLiteral("Layer name"), languageCode_), QLineEdit::Normal, currentName, &accepted);
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
	connect(layerUpButton_, &QPushButton::clicked, this, [this] { canvas_->moveSelectedLayer(-1); });
	connect(layerDownButton_, &QPushButton::clicked, this, [this] { canvas_->moveSelectedLayer(1); });
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
			label += QStringLiteral(" · ") + localizedText(QStringLiteral("hidden"), languageCode_);
		if (layer.locked)
			label += QStringLiteral(" · ") + localizedText(QStringLiteral("locked"), languageCode_);
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
			setLocalizedText(visibilityLayerButton_, layer.visible ? QStringLiteral("Hide") : QStringLiteral("Show"), languageCode_);
			setLocalizedText(lockLayerButton_, layer.locked ? QStringLiteral("Unlock") : QStringLiteral("Lock"), languageCode_);
			deleteLayerButton_->setEnabled(roundActive_ && currentDrawer_ == username_
				&& currentLayers.size() > 1 && !layer.locked);
		}
	}
	updateRoundControls();
}

void MainWindow::chooseTextAt(const QPointF& position)
{
	bool accepted = false;
	const QString text = QInputDialog::getMultiLineText(this, localizedText(QStringLiteral("Add text"), languageCode_),
		localizedText(QStringLiteral("Text to place on the canvas"), languageCode_), QString(), &accepted);
	if (accepted && !text.trimmed().isEmpty())
		canvas_->addTextAt(position, text);
}

void MainWindow::chooseColor()
{
	const QColor initial(colorButton_->property("canvasColor").toString().isEmpty()
		? QStringLiteral("#202124") : colorButton_->property("canvasColor").toString());
	const QColor color = QColorDialog::getColor(initial, this, localizedText(QStringLiteral("Choose drawing color"), languageCode_));
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
		loginStatusLabel_->setText(localizedText(QStringLiteral("Waiting for the master service connection…"), languageCode_));
		connectMaster();
		return;
	}
	const QString username = usernameEdit_->text().trimmed();
	const QString password = passwordEdit_->text();
	if (username.isEmpty() || password.isEmpty()) {
		loginStatusLabel_->setText(localizedText(QStringLiteral("Enter a username and password."), languageCode_));
		return;
	}
	const bool create = createAccountCheck_->isChecked();
	if (create && displayNameEdit_->text().trimmed().isEmpty()) {
		loginStatusLabel_->setText(localizedText(QStringLiteral("Enter a display name for your new account."), languageCode_));
		return;
	}
	const QString type = create ? QStringLiteral("CreateAccount") : QStringLiteral("Login");
	QJsonObject fields{{QStringLiteral("requestId"), nextRequestId()},
					   {QStringLiteral("username"), username},
					   {QStringLiteral("password"), password}};
	if (create)
		fields.insert(QStringLiteral("displayName"), displayNameEdit_->text().trimmed());
	pendingAuthRequestId_ = fields.value(QStringLiteral("requestId")).toString();
	loginStatusLabel_->setText(localizedText(QStringLiteral("Contacting master…"), languageCode_));
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
		loginStatusLabel_->setText(localizedText(
			QStringLiteral("You were kicked out because someone else logged in to this account."), languageCode_));
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
			setLocalizedFormattedText(account, QStringLiteral("Signed in as %1 · %2 points"),
				{profile.displayName, QString::number(profile.totalScore)}, languageCode_);
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
			profileStatusLabel_->setText(localizedText(QStringLiteral("Profile saved."), languageCode_));
		if (auto* account = lobbyPage_->findChild<QLabel*>(QStringLiteral("accountLabel")))
			setLocalizedFormattedText(account, QStringLiteral("Signed in as %1 · %2 points"),
				{profile.displayName, QString::number(profile.totalScore)}, languageCode_);
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
		const int points = drawingPoints ? profile.drawingScore : profile.guessingScore;
		table->setItem(row, 0, new QTableWidgetItem(QString::number(rank)));
		table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1 (@%2)").arg(profile.displayName, profile.username)));
		auto* countryItem = new QTableWidgetItem(profile.countryCode.isEmpty()
			? localizedText(QStringLiteral("Country not set"), languageCode_) : profile.countryCode);
		countryItem->setIcon(countryFlagIcon(profile.countryCode));
		table->setItem(row, 2, countryItem);
		table->item(row, 2)->setToolTip(profile.countryCode.isEmpty()
			? localizedText(QStringLiteral("Country not set"), languageCode_) : profile.countryCode);
		table->setItem(row, 3, new QTableWidgetItem(QString::number(points)));
	}
}

void MainWindow::saveProfile()
{
	if (authToken_.isEmpty() || masterSocket_.state() != QAbstractSocket::ConnectedState)
		return;
	if (currentPasswordEdit_->text().isEmpty() != newPasswordEdit_->text().isEmpty()) {
		profileStatusLabel_->setText(localizedText(
			QStringLiteral("Enter both current and new passwords to change your password."), languageCode_));
		return;
	}
	sendMessage(&masterSocket_, makeMessage(QStringLiteral("UpdateProfile"),
		{{QStringLiteral("requestId"), nextRequestId()}, {QStringLiteral("authToken"), authToken_},
		 {QStringLiteral("displayName"), nicknameEdit_->text()},
		 {QStringLiteral("countryCode"), countryCombo_->currentData().toString()},
		 {QStringLiteral("currentPassword"), currentPasswordEdit_->text()},
		 {QStringLiteral("newPassword"), newPasswordEdit_->text()}}));
	profileStatusLabel_->setText(localizedText(QStringLiteral("Saving profile…"), languageCode_));
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
		setLocalizedFormattedText(gameRoomLabel_, QStringLiteral("Room: %1"),
			{joiningRoom_.roomName}, languageCode_);
		canvas_->setScene(message.value(QStringLiteral("layers")).toArray(),
			message.value(QStringLiteral("objects")).toArray());
		pages_->setCurrentWidget(gamePage_);
		updateRoster(message.value(QStringLiteral("players")).toArray(), room);
		updateQueueStatus(message.value(QStringLiteral("queue")).toObject());
		setLocalizedText(roundStatusLabel_, QStringLiteral("Waiting for players"), languageCode_);
		appendGameMessage(localizedText(QStringLiteral("Joined %1 as %2"), languageCode_)
			.arg(joiningRoom_.roomName, localizedText(isSpectator_ ? QStringLiteral("spectator")
				: QStringLiteral("player"), languageCode_)));
		if (message.value(QStringLiteral("roundActive")).toBool()) {
			currentDrawer_ = message.value(QStringLiteral("drawerUsername")).toString();
			secondsRemaining_ = message.value(QStringLiteral("secondsRemaining")).toInt();
			roundActive_ = true;
			updateRoster(message.value(QStringLiteral("players")).toArray(), {});
			setLocalizedFormattedText(roundStatusLabel_, QStringLiteral("Round in progress · drawing: %1"),
				{currentDrawer_}, languageCode_);
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
		appendGameMessage(localizedText(QStringLiteral("A player slot opened; you joined the players"), languageCode_));
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
			appendGameMessage(localizedText(QStringLiteral("You are waiting for a player slot"), languageCode_));
		} else {
			queuedForPlayer_ = false;
			updateQueueStatus({});
			appendGameMessage(localizedText(isSpectator_ ? QStringLiteral("You are now spectating")
				: QStringLiteral("You joined the players"), languageCode_));
		}
		if (roundActive_)
			setLocalizedFormattedText(roundStatusLabel_, QStringLiteral("Round in progress · drawing: %1"),
				{currentDrawer_}, languageCode_);
		updateRoundControls();
	} else if (type == QStringLiteral("RoundStarted")) {
		roundActive_ = true;
		currentDrawer_ = message.value(QStringLiteral("drawerUsername")).toString();
		updateRoster(roomRoster_, {});
		secondsRemaining_ = message.value(QStringLiteral("secondsRemaining")).toInt();
		setLocalizedText(wordLabel_, username_ == currentDrawer_ ? QStringLiteral("Your word: …")
			: QStringLiteral("Your word: —"), languageCode_);
		setLocalizedFormattedText(roundStatusLabel_, QStringLiteral("Round %1 · %2 is drawing"),
			{QString::number(message.value(QStringLiteral("round")).toInt()), currentDrawer_}, languageCode_);
		updateRoundControls();
	} else if (type == QStringLiteral("WordAssigned")) {
		setLocalizedFormattedText(wordLabel_, QStringLiteral("Your word: %1"),
			{message.value(QStringLiteral("word")).toString()}, languageCode_);
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
		setLocalizedFormattedText(wordLabel_, QStringLiteral("Answer: %1"),
			{message.value(QStringLiteral("word")).toString()}, languageCode_);
		setLocalizedText(roundStatusLabel_, QStringLiteral("Round ended"), languageCode_);
		appendGameMessage(localizedText(QStringLiteral("The word was %1"), languageCode_)
			.arg(message.value(QStringLiteral("word")).toString()));
		updateRoundControls();
	} else if (type == QStringLiteral("RoundPaused")) {
		roundActive_ = false;
		updateRoster(roomRoster_, {});
		roundStatusLabel_->setText(localizedText(message.value(QStringLiteral("message")).toString(), languageCode_));
		updateRoundControls();
	} else if (type == QStringLiteral("GuessCorrect")) {
		appendGameMessage(localizedText(QStringLiteral("%1 guessed correctly in place %2 (+%3 points)"), languageCode_)
			.arg(message.value(QStringLiteral("displayName")).toString())
			.arg(message.value(QStringLiteral("placement")).toInt())
			.arg(message.value(QStringLiteral("points")).toInt()));
	} else if (type == QStringLiteral("ChatMessage")) {
		appendGameMessage(QStringLiteral("%1: %2")
			.arg(message.value(QStringLiteral("displayName")).toString(),
				 message.value(QStringLiteral("text")).toString()));
	} else if (type == QStringLiteral("PlayerLeft")) {
		appendGameMessage(localizedText(QStringLiteral("%1 left the room"), languageCode_)
			.arg(message.value(QStringLiteral("username")).toString()));
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
		auto* occupancyItem = new QTableWidgetItem;
		occupancyItem->setData(Qt::UserRole, QStringLiteral("%1 / %2 · %3 spectating"));
		occupancyItem->setData(Qt::UserRole + 1, QStringList{QString::number(room.players), QString::number(room.maxPlayers), QString::number(room.spectators)});
		occupancyItem->setText([&] {
			QString text = localizedText(QStringLiteral("%1 / %2 · %3 spectating"), languageCode_);
			for (const QString& argument : occupancyItem->data(Qt::UserRole + 1).toStringList())
				text = text.arg(argument);
			return text;
		}());
		roomsTable_->setItem(row, 1, occupancyItem);
		auto* durationItem = new QTableWidgetItem;
		durationItem->setData(Qt::UserRole, QStringLiteral("%1 sec"));
		durationItem->setData(Qt::UserRole + 1, QStringList{QString::number(room.roundSeconds)});
		durationItem->setText(localizedText(QStringLiteral("%1 sec"), languageCode_).arg(room.roundSeconds));
		roomsTable_->setItem(row, 2, durationItem);
		auto* latencyIndicator = new LatencyIndicator(roomsTable_);
		latencyIndicator->setLanguageCode(languageCode_);
		latencyIndicator->setPing(roomPingMs_.value(room.roomId, -1));
		roomLatencyIndicators_.insert(room.roomId, latencyIndicator);
		roomsTable_->setCellWidget(row, 3, latencyIndicator);
		roomsTable_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1:%2").arg(room.host).arg(room.port)));
	}
	roomsRefreshElapsed_.start();
		setLocalizedFormattedText(connectionStatusLabel_, QStringLiteral("%1 room(s) listed"),
			{QString::number(roomsTable_->rowCount())}, languageCode_);
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
		|| connectionStatusLabel_->property("gtdSourceText").toString()
			!= QStringLiteral("%1 room(s) listed"))
		return;
	const qint64 secondsAgo = roomsRefreshElapsed_.elapsed() / 1000;
	setLocalizedFormattedText(connectionStatusLabel_,
		QStringLiteral("%1 room(s) listed · refreshed %2 second%3 ago"),
		{QString::number(roomsTable_->rowCount()), QString::number(secondsAgo),
		 secondsAgo == 1 ? QString() : QStringLiteral("s")}, languageCode_);
}

void MainWindow::joinSelectedRoom()
{
	const int row = roomsTable_->currentRow();
	if (row < 0 || !roomsTable_->item(row, 0))
		return;
	const QString roomId = roomsTable_->item(row, 0)->data(Qt::UserRole).toString();
	joiningRoom_ = roomsById_.value(roomId);
	if (joiningRoom_.roomId.isEmpty() || joiningRoom_.host.isEmpty() || joiningRoom_.port == 0) {
			setLocalizedText(connectionStatusLabel_, QStringLiteral("Selected room has an invalid server address"), languageCode_);
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
		setLocalizedFormattedText(connectionStatusLabel_, QStringLiteral("Connecting to %1…"),
			{joiningRoom_.roomName}, languageCode_);
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
		setLocalizedText(voiceStatusLabel_, QStringLiteral("No microphone device"), languageCode_);
		return;
	}
	const QAudioFormat format = input.preferredFormat();
	if (!usableAudioFormat(format) || !input.isFormatSupported(format)) {
		stopVoiceInput();
		setLocalizedText(voiceStatusLabel_, QStringLiteral("Unsupported microphone format"), languageCode_);
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
		setLocalizedText(voiceStatusLabel_, QStringLiteral("Could not start microphone"), languageCode_);
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
			setLocalizedText(voiceStatusLabel_, QStringLiteral("Microphone stopped unexpectedly"), languageCode_);
		}
	});
	setLocalizedText(voiceButton_, QStringLiteral("Disable microphone"), languageCode_);
	setLocalizedText(voiceStatusLabel_, QStringLiteral("Microphone on"), languageCode_);
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
		setLocalizedText(voiceButton_, QStringLiteral("Enable microphone"), languageCode_);
	}
	if (voiceStatusLabel_) {
		if (inRoom_)
			setLocalizedText(voiceStatusLabel_, QStringLiteral("Microphone off"), languageCode_);
		else
			voiceStatusLabel_->clear();
	}
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
			setLocalizedText(voiceStatusLabel_, QStringLiteral("No audio output device"), languageCode_);
			return;
		}
		const QAudioFormat format = output.preferredFormat();
		if (!usableAudioFormat(format) || !output.isFormatSupported(format)) {
			setLocalizedText(voiceStatusLabel_, QStringLiteral("Unsupported audio output format"), languageCode_);
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
			setLocalizedText(voiceStatusLabel_, QStringLiteral("Could not start audio playback"), languageCode_);
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
	setLocalizedText(promoteToPlayerButton_, queuedForPlayer_ ? QStringLiteral("Cancel Wait")
		: isSpectator_ ? QStringLiteral("Join as Player") : QStringLiteral("Switch to Spectator"), languageCode_);
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
	setLocalizedPlaceholder(roomChatEdit_, isDrawer
		? QStringLiteral("Drawer can listen but not send") : QStringLiteral("Send a room message"), languageCode_);
	sendChatButton_->setEnabled(inRoom_ && !isDrawer);
	voiceButton_->setEnabled(inRoom_ && !isDrawer);
	setLocalizedText(voiceButton_, isDrawer ? QStringLiteral("Drawer: receive only")
		: voiceButton_->isChecked() ? QStringLiteral("Disable microphone") : QStringLiteral("Enable microphone"), languageCode_);
	if (isDrawer)
		setLocalizedText(voiceStatusLabel_, QStringLiteral("You can hear room voice chat"), languageCode_);
	else if (!voiceButton_->isChecked()) {
		if (inRoom_)
			setLocalizedText(voiceStatusLabel_, QStringLiteral("Microphone off"), languageCode_);
		else
			voiceStatusLabel_->clear();
	}
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
	setLocalizedFormattedText(gameRoomLabel_, QStringLiteral("Room: %1 · Players: %2/%3 · Spectators: %4"),
		{joiningRoom_.roomName, QString::number(joiningRoom_.players),
		 QString::number(joiningRoom_.maxPlayers), QString::number(joiningRoom_.spectators)}, languageCode_);
	rosterList_->clear();
	for (const QJsonValue& value : players) {
		const QJsonObject entry = value.toObject();
		const PlayerProfile profile = PlayerProfile::fromJson(entry.value(QStringLiteral("profile")).toObject());
		const QString role = entry.value(QStringLiteral("spectator")).toBool()
			? localizedText(QStringLiteral(" · Spectator"), languageCode_) : QString();
		const QString drawing = roundActive_ && profile.username == currentDrawer_
			? localizedText(QStringLiteral(" · Drawing"), languageCode_) : QString();
		QString rosterText = localizedText(QStringLiteral("%1%2%3 (@%4)\nRoom: %5 · Total: %6"), languageCode_);
		rosterText = rosterText.arg(profile.displayName, role, drawing, profile.username)
			.arg(entry.value(QStringLiteral("roomScore")).toInt()).arg(profile.totalScore);
		const QString countryText = profile.countryCode.isEmpty()
			? localizedText(QStringLiteral("Country not set"), languageCode_) : profile.countryCode;
		rosterText.insert(rosterText.indexOf(QLatin1Char('\n')), QStringLiteral(" · %1").arg(countryText));
		auto* rosterItem = new QListWidgetItem(rosterText, rosterList_);
		rosterItem->setIcon(countryFlagIcon(profile.countryCode));
		const QString globalRank = profile.leaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.leaderboardRank)
			: localizedText(QStringLiteral("Unranked"), languageCode_);
		const QString guessingRank = profile.guessingRank.isEmpty()
			? localizedText(QStringLiteral("Unrated"), languageCode_) : profile.guessingRank;
		const QString drawingRank = profile.drawingRank.isEmpty()
			? localizedText(QStringLiteral("Unrated"), languageCode_) : profile.drawingRank;
		const QString guessingLeaderboardRank = profile.guessingLeaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.guessingLeaderboardRank) : localizedText(QStringLiteral("Unranked"), languageCode_);
		const QString drawingLeaderboardRank = profile.drawingLeaderboardRank > 0
			? QStringLiteral("#%1").arg(profile.drawingLeaderboardRank) : localizedText(QStringLiteral("Unranked"), languageCode_);
		const QString ratio = profile.roundsLost > 0
			? QString::number(static_cast<double>(profile.roundsWon) / static_cast<double>(profile.roundsLost), 'f', 2)
			: (profile.roundsWon > 0 ? QStringLiteral("∞")
				: localizedText(QStringLiteral("N/A"), languageCode_));
		QString rosterToolTip = localizedText(QStringLiteral("%1 (@%2)\nGuessing: %3 points · %4 · Rank %5\nDrawing: %6 points · %7 · Rank %8\nCombined: %9 points · Rank %10\nWins: %11\nLosses: %12\nWin/Loss ratio: %13"), languageCode_);
		rosterToolTip = rosterToolTip.arg(profile.displayName, profile.username)
			.arg(profile.guessingScore).arg(guessingRank, guessingLeaderboardRank)
			.arg(profile.drawingScore).arg(drawingRank, drawingLeaderboardRank)
			.arg(profile.totalScore).arg(globalRank).arg(profile.roundsWon).arg(profile.roundsLost).arg(ratio);
		rosterToolTip = rosterToolTip.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
		if (profile.countryCode.isEmpty()) {
			rosterToolTip += QStringLiteral("<br>%1")
				.arg(localizedText(QStringLiteral("Country not set"), languageCode_).toHtmlEscaped());
		} else {
			const QString countryCode = profile.countryCode.toLower();
			rosterToolTip += QStringLiteral("<br>Country: <img src=\":/country-flags/%1.png\" width=\"24\" height=\"16\"> %2")
				.arg(countryCode, profile.countryCode.toHtmlEscaped());
		}
		rosterItem->setToolTip(rosterToolTip);
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
	setLocalizedFormattedText(queueStatusLabel_,
		QStringLiteral("Waiting for a player slot · %1 ahead · position %2 of %3"),
		{QString::number(ahead), QString::number(position), QString::number(total)}, languageCode_);
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
