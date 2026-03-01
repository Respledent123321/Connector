#pragma once

#include "ConfigManager.h"
#include "MemoryStore.h"

#include <QString>
#include <QVector>

class ReasonerEngine {
public:
    void setConfig(const AppConfig &config);

    QString generateReply(const QString &userText,
                          const QVector<ConversationEntry> &recent,
                          const QVector<MemoryEntry> &memories,
                          const QString &researchSummary) const;

private:
    AppConfig m_config;

    QString runLlamaOnce(const QString &systemPrompt, const QString &userPrompt, int nPredict) const;
    QString generateWithLlamaCli(const QString &prompt) const;
};
