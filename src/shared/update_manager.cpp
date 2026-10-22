#include "shared/update_manager.h"

#include "shared/app_version.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>

namespace gtd {
namespace {

const QUrl ManifestUrl(QStringLiteral(
	"https://github.com/WingsOfAltair/GuessTheDrawing-Multiplayer-QtCPP/releases/latest/download/gtd-update.json"));
const QUrl PackageUrl(QStringLiteral(
	"https://github.com/WingsOfAltair/GuessTheDrawing-Multiplayer-QtCPP/releases/latest/download/gtd-windows-x64.zip"));
constexpr qsizetype MaximumManifestBytes = 64 * 1024;
constexpr qsizetype MaximumPackageBytes = 256 * 1024 * 1024;

QNetworkRequest secureRequest(const QUrl& url)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("GuessTheDrawing/%1").arg(ApplicationVersion));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(60000);
	return request;
}

QString encodeBase64(const QByteArray& value)
{
	return QString::fromLatin1(value.toBase64());
}

}

UpdateManager::UpdateManager(QObject* parent)
	: QObject(parent), network_(new QNetworkAccessManager(this))
{
}

void UpdateManager::checkForUpdates()
{
#ifdef Q_OS_WIN
	if (checking_)
		return;
	checking_ = true;
	QNetworkReply* reply = network_->get(secureRequest(ManifestUrl));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QByteArray manifestData = reply->readAll();
		const QNetworkReply::NetworkError networkError = reply->error();
		const QString errorText = reply->errorString();
		const QUrl responseUrl = reply->url();
		reply->deleteLater();
		if (networkError != QNetworkReply::NoError || responseUrl.scheme() != QStringLiteral("https")
			|| manifestData.isEmpty() || manifestData.size() > MaximumManifestBytes) {
			checking_ = false;
			emit checkFailed(networkError == QNetworkReply::NoError
				? QStringLiteral("The update manifest was invalid or too large") : errorText);
			return;
		}

		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(manifestData, &parseError);
		const QJsonObject manifest = document.object();
		const QString versionText = manifest.value(QStringLiteral("version")).toString();
		const QString sha256 = manifest.value(QStringLiteral("sha256")).toString().toLower();
		int suffixIndex = -1;
		const QVersionNumber remoteVersion = QVersionNumber::fromString(versionText, &suffixIndex);
		const QVersionNumber localVersion = QVersionNumber::fromString(ApplicationVersion);
		static const QRegularExpression versionPattern(QStringLiteral("^[0-9]+(?:\\.[0-9]+){2}$"));
		static const QRegularExpression sha256Pattern(QStringLiteral("^[0-9a-f]{64}$"));
		if (parseError.error != QJsonParseError::NoError || !document.isObject()
			|| !versionPattern.match(versionText).hasMatch() || suffixIndex != versionText.size()
			|| !sha256Pattern.match(sha256).hasMatch()) {
			checking_ = false;
			emit checkFailed(QStringLiteral("The update manifest has invalid version or checksum data"));
			return;
		}
		if (QVersionNumber::compare(remoteVersion, localVersion) <= 0) {
			checking_ = false;
			return;
		}
		downloadPackage(versionText, QByteArray::fromHex(sha256.toLatin1()));
	});
#else
	Q_UNUSED(this);
#endif
}

void UpdateManager::downloadPackage(const QString& version, const QByteArray& expectedSha256)
{
	QNetworkReply* reply = network_->get(secureRequest(PackageUrl));
	connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
		if (received > MaximumPackageBytes || total > MaximumPackageBytes)
			reply->abort();
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, version, expectedSha256] {
		const QByteArray packageData = reply->readAll();
		const QNetworkReply::NetworkError networkError = reply->error();
		const QString errorText = reply->errorString();
		const QUrl responseUrl = reply->url();
		reply->deleteLater();
		checking_ = false;
		if (networkError != QNetworkReply::NoError || responseUrl.scheme() != QStringLiteral("https")
			|| packageData.isEmpty() || packageData.size() > MaximumPackageBytes) {
			emit checkFailed(networkError == QNetworkReply::NoError
				? QStringLiteral("The update package was invalid or exceeded the size limit") : errorText);
			return;
		}
		if (QCryptographicHash::hash(packageData, QCryptographicHash::Sha256) != expectedSha256) {
			emit checkFailed(QStringLiteral("The update package checksum did not match the release manifest"));
			return;
		}

		const QString updatesDirectory = QDir(QStandardPaths::writableLocation(
			QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("updates"));
		if (!QDir().mkpath(updatesDirectory)) {
			emit checkFailed(QStringLiteral("Could not create the local update staging directory"));
			return;
		}
		const QString archivePath = QDir(updatesDirectory).filePath(
			QStringLiteral("gtd-windows-x64-%1.zip").arg(version));
		QSaveFile archive(archivePath);
		if (!archive.open(QIODevice::WriteOnly) || archive.write(packageData) != packageData.size()
			|| !archive.commit()) {
			emit checkFailed(QStringLiteral("Could not save the verified update package"));
			return;
		}
		emit updateAvailable(version, archivePath);
	});
}

bool UpdateManager::installAndRestart(const QString& archivePath, QString* error) const
{
#ifdef Q_OS_WIN
	const QString updaterDirectory = QDir(QStandardPaths::writableLocation(
		QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("updates"));
	if (!QDir().mkpath(updaterDirectory)) {
		if (error)
			*error = QStringLiteral("Could not create the update helper directory");
		return false;
	}
	const QString executablePath = QCoreApplication::applicationFilePath();
	const QString installDirectory = QCoreApplication::applicationDirPath();
	const QString scriptPath = QDir(updaterDirectory).filePath(QStringLiteral("install-update.ps1"));
	QJsonArray restartArguments;
	const QStringList applicationArguments = QCoreApplication::arguments();
	for (qsizetype index = 1; index < applicationArguments.size(); ++index)
		restartArguments.append(applicationArguments.at(index));

	const QString script = QStringLiteral(
		"$ErrorActionPreference = 'Stop'\n"
		"function Decode([string]$value) { [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($value)) }\n"
		"$parentPid = %1\n"
		"$archive = Decode '%2'\n"
		"$installDir = Decode '%3'\n"
		"$executable = Decode '%4'\n"
		"$restartArgs = @(Decode '%5' | ConvertFrom-Json)\n"
		"$stageDir = Join-Path $env:TEMP ('gtd-update-' + [Guid]::NewGuid().ToString('N'))\n"
		"$logPath = Join-Path $env:LOCALAPPDATA 'GuessTheDrawing\\update-error.log'\n"
		"try {\n"
		"  Wait-Process -Id $parentPid -ErrorAction SilentlyContinue\n"
		"  New-Item -ItemType Directory -Path $stageDir -Force | Out-Null\n"
		"  Expand-Archive -LiteralPath $archive -DestinationPath $stageDir -Force\n"
		"  if (-not (Test-Path -LiteralPath (Join-Path $stageDir (Split-Path $executable -Leaf)))) { throw 'The package does not contain the running application.' }\n"
		"  Get-ChildItem -LiteralPath $stageDir -Force | Copy-Item -Destination $installDir -Recurse -Force\n"
		"  function Quote-Arg([string]$value) { $escaped = $value -replace '(\\\\*)\"', '$1$1\\\"'; $escaped = $escaped -replace '(\\\\+)$', '$1$1'; return '\"' + $escaped + '\"' }\n"
		"  $argumentLine = ($restartArgs | ForEach-Object { Quote-Arg ([string]$_) }) -join ' '\n"
		"  Start-Process -FilePath $executable -ArgumentList $argumentLine -WorkingDirectory $installDir\n"
		"} catch {\n"
		"  New-Item -ItemType Directory -Path (Split-Path $logPath) -Force | Out-Null\n"
		"  [IO.File]::AppendAllText($logPath, ([DateTime]::UtcNow.ToString('o') + ' ' + $_.ToString() + [Environment]::NewLine))\n"
		"} finally {\n"
		"  if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue }\n"
		"}\n")
		.arg(QCoreApplication::applicationPid())
		.arg(encodeBase64(archivePath.toUtf8()), encodeBase64(installDirectory.toUtf8()),
			encodeBase64(executablePath.toUtf8()),
			encodeBase64(QJsonDocument(restartArguments).toJson(QJsonDocument::Compact)));

	QSaveFile scriptFile(scriptPath);
	if (!scriptFile.open(QIODevice::WriteOnly) || scriptFile.write(script.toUtf8()) != script.toUtf8().size()
		|| !scriptFile.commit()) {
		if (error)
			*error = QStringLiteral("Could not create the update installer script");
		return false;
	}
	qint64 detachedPid = 0;
	if (!QProcess::startDetached(QStringLiteral("powershell.exe"),
		{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
		 QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
		 QStringLiteral("-File"), scriptPath}, updaterDirectory, &detachedPid)) {
		if (error)
			*error = QStringLiteral("Could not launch the update installer");
		return false;
	}
	return true;
#else
	Q_UNUSED(archivePath);
	if (error)
		*error = QStringLiteral("Automatic installation is only available for Windows release packages");
	return false;
#endif
}

}
