#pragma once

#include <QString>
#include <QStringList>

struct AppConfig {
    QString llamaCliPath;
    QString llamaModelPath;
    QString llmProfile = "balanced";
    QString whisperCliPath;
    QString whisperModelPath;
    QString piperPath;
    QString piperModelPath;
    QString piperConfigPath;
    int llamaNPredict = 128;
    double llamaTemperature = 0.4;
    int llamaThreads = 0;

    bool researchEnabled = true;
    bool speakResponses = false;
    bool readOnlyWorkspace = true;
    int ggwaveTxVolume = 20;
    QString ggwaveProtocol = "audible_fast";
    int ggwaveRepeatCount = 1;
    bool ggwaveRouteToChat = false;

    QStringList toolAllowlist;
};

class ConfigManager {
public:
    bool load(const QString &path);
    bool save(const QString &path) const;

    const AppConfig &config() const;
    AppConfig &config();

private:
    AppConfig m_config;

    static QString parseScalar(const QString &line);
    static QStringList parseList(const QString &value);
};
