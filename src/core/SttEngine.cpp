#include "SttEngine.h"

#include "AudioUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

void SttEngine::setConfig(const AppConfig &config) {
    m_config = config;
}

QString SttEngine::transcribePcm16Mono(const QByteArray &pcm16, int sampleRate) const {
    if (pcm16.isEmpty()) {
        return QString();
    }

    QFileInfo whisperCli(m_config.whisperCliPath);
    QFileInfo whisperModel(m_config.whisperModelPath);

    if (!whisperCli.exists() || !whisperModel.exists()) {
        const double seconds = static_cast<double>(pcm16.size()) / (sampleRate * 2.0);
        return QString("[STT not configured. Captured %1s audio. Configure whisper_cli_path and whisper_model_path in settings.]")
            .arg(seconds, 0, 'f', 2);
    }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        return "[STT failed: unable to create temporary directory.]";
    }

    const QString wavPath = QDir(dir.path()).filePath("input.wav");
    if (!AudioUtils::writePcm16Wav(wavPath, pcm16, sampleRate)) {
        return "[STT failed: unable to write temporary WAV file.]";
    }

    const QString outPrefix = QDir(dir.path()).filePath("result");

    QProcess process;
    QStringList args;
    args << "-m" << whisperModel.absoluteFilePath()
         << "-f" << wavPath
         << "-otxt"
         << "-nt"
         << "-of" << outPrefix;

    process.start(whisperCli.absoluteFilePath(), args);
    if (!process.waitForStarted(4000)) {
        return "[STT failed: could not start whisper process.]";
    }

    if (!process.waitForFinished(180000)) {
        process.kill();
        return "[STT failed: whisper process timed out.]";
    }

    const QString txtPath = outPrefix + ".txt";
    QFile txt(txtPath);
    if (!txt.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString fallback = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!fallback.isEmpty()) {
            return fallback;
        }
        return "[STT produced no transcript file.]";
    }

    const QString transcript = QString::fromUtf8(txt.readAll()).trimmed();
    if (transcript.isEmpty()) {
        return "[STT transcript was empty.]";
    }

    return transcript;
}
