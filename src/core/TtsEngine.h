#pragma once

#include "ConfigManager.h"

#include <QObject>

class QAudioOutput;
class QMediaPlayer;

class TtsEngine : public QObject {
    Q_OBJECT

public:
    explicit TtsEngine(QObject *parent = nullptr);

    void setConfig(const AppConfig &config);
    bool speak(const QString &text);

private:
    AppConfig m_config;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;

    bool speakWithPiper(const QString &text);
    bool speakWithWindowsFallback(const QString &text);
};
