#include "TtsEngine.h"

#include "AppPaths.h"

#include <QAudioOutput>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
#include <QUrl>

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent),
      m_player(new QMediaPlayer(this)),
      m_audioOutput(new QAudioOutput(this)) {
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8f);
}

void TtsEngine::setConfig(const AppConfig &config) {
    m_config = config;
}

bool TtsEngine::speak(const QString &text) {
    if (text.trimmed().isEmpty()) {
        return false;
    }

    if (speakWithPiper(text)) {
        return true;
    }

    return speakWithWindowsFallback(text);
}

bool TtsEngine::speakWithPiper(const QString &text) {
    QFileInfo piperExe(m_config.piperPath);
    QFileInfo piperModel(m_config.piperModelPath);
    if (!piperExe.exists() || !piperModel.exists()) {
        return false;
    }

    QDir cacheDir(QDir(AppPaths::dataDir()).filePath("cache"));
    cacheDir.mkpath(".");
    const QString wavPath = cacheDir.filePath(
        "tts-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmsszzz") + ".wav");

    QProcess process;
    QStringList args;
    args << "--model" << piperModel.absoluteFilePath();

    if (!m_config.piperConfigPath.trimmed().isEmpty()) {
        args << "--config" << m_config.piperConfigPath;
    }

    args << "--output_file" << wavPath;

    process.start(piperExe.absoluteFilePath(), args);
    if (!process.waitForStarted(4000)) {
        return false;
    }

    process.write(text.toUtf8());
    process.closeWriteChannel();

    if (!process.waitForFinished(60000)) {
        process.kill();
        return false;
    }

    if (!QFileInfo::exists(wavPath)) {
        return false;
    }

    m_player->setSource(QUrl::fromLocalFile(wavPath));
    m_player->play();
    return true;
}

bool TtsEngine::speakWithWindowsFallback(const QString &text) {
#ifdef Q_OS_WIN
    QString escaped = text;
    escaped.replace("'", "''");

    const QString command =
        "Add-Type -AssemblyName System.Speech; "
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$s.Speak('" + escaped + "');";

    return QProcess::startDetached("powershell", {"-NoProfile", "-Command", command});
#else
    Q_UNUSED(text);
    return false;
#endif
}
