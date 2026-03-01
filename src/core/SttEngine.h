#pragma once

#include "ConfigManager.h"

#include <QString>

class SttEngine {
public:
    void setConfig(const AppConfig &config);

    QString transcribePcm16Mono(const QByteArray &pcm16, int sampleRate) const;

private:
    AppConfig m_config;
};
