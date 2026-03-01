#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

struct ConversationEntry {
    int id = -1;
    QDateTime timestamp;
    QString role;
    QString text;
    QString speaker;
    QString sessionId;
};

struct MemoryEntry {
    int id = -1;
    QDateTime timestamp;
    QString category;
    QString content;
};

struct VoiceSignature {
    double rms = 0.0;
    double zcr = 0.0;
};

class MemoryStore : public QObject {
    Q_OBJECT

public:
    explicit MemoryStore(QObject *parent = nullptr);
    ~MemoryStore() override;

    bool initialize(const QString &dbPath);

    bool addConversation(const QString &role,
                         const QString &text,
                         const QString &speaker,
                         const QString &sessionId);

    QVector<ConversationEntry> recentConversations(int limit) const;

    bool addMemory(const QString &category, const QString &content);
    QVector<MemoryEntry> listMemories(int limit) const;
    bool forgetMemory(int id);

    QString identifyOrCreateSpeaker(const VoiceSignature &sig);

private:
    QString m_connectionName;
};
