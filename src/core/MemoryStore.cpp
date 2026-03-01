#include "MemoryStore.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
QSqlDatabase openDb(const QString &name) {
    if (QSqlDatabase::contains(name)) {
        return QSqlDatabase::database(name);
    }
    return QSqlDatabase();
}
}

MemoryStore::MemoryStore(QObject *parent)
    : QObject(parent),
      m_connectionName("robot-db-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

MemoryStore::~MemoryStore() {
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool MemoryStore::initialize(const QString &dbPath) {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        return false;
    }

    QSqlQuery q(db);

    const char *ddl[] = {
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "ts TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "text TEXT NOT NULL,"
        "speaker TEXT NOT NULL,"
        "session_id TEXT NOT NULL"
        ")",
        "CREATE TABLE IF NOT EXISTS memories ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "ts TEXT NOT NULL,"
        "category TEXT NOT NULL,"
        "content TEXT NOT NULL"
        ")",
        "CREATE TABLE IF NOT EXISTS speaker_profiles ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "label TEXT NOT NULL UNIQUE,"
        "rms REAL NOT NULL,"
        "zcr REAL NOT NULL,"
        "count INTEGER NOT NULL DEFAULT 1,"
        "last_seen TEXT NOT NULL"
        ")"};

    for (const char *stmt : ddl) {
        if (!q.exec(stmt)) {
            return false;
        }
    }

    return true;
}

bool MemoryStore::addConversation(const QString &role,
                                  const QString &text,
                                  const QString &speaker,
                                  const QString &sessionId) {
    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO conversations(ts, role, text, speaker, session_id) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(role);
    q.addBindValue(text);
    q.addBindValue(speaker.isEmpty() ? "unknown" : speaker);
    q.addBindValue(sessionId);
    return q.exec();
}

QVector<ConversationEntry> MemoryStore::recentConversations(int limit) const {
    QVector<ConversationEntry> out;

    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return out;
    }

    QSqlQuery q(db);
    q.prepare("SELECT id, ts, role, text, speaker, session_id FROM conversations ORDER BY id DESC LIMIT ?");
    q.addBindValue(limit);

    if (!q.exec()) {
        return out;
    }

    while (q.next()) {
        ConversationEntry e;
        e.id = q.value(0).toInt();
        e.timestamp = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        e.role = q.value(2).toString();
        e.text = q.value(3).toString();
        e.speaker = q.value(4).toString();
        e.sessionId = q.value(5).toString();
        out.push_back(e);
    }

    std::reverse(out.begin(), out.end());
    return out;
}

bool MemoryStore::addMemory(const QString &category, const QString &content) {
    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO memories(ts, category, content) VALUES (?, ?, ?)");
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(category);
    q.addBindValue(content);
    return q.exec();
}

QVector<MemoryEntry> MemoryStore::listMemories(int limit) const {
    QVector<MemoryEntry> out;

    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return out;
    }

    QSqlQuery q(db);
    q.prepare("SELECT id, ts, category, content FROM memories ORDER BY id DESC LIMIT ?");
    q.addBindValue(limit);

    if (!q.exec()) {
        return out;
    }

    while (q.next()) {
        MemoryEntry e;
        e.id = q.value(0).toInt();
        e.timestamp = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
        e.category = q.value(2).toString();
        e.content = q.value(3).toString();
        out.push_back(e);
    }

    return out;
}

bool MemoryStore::forgetMemory(int id) {
    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }

    QSqlQuery q(db);
    q.prepare("DELETE FROM memories WHERE id = ?");
    q.addBindValue(id);
    return q.exec();
}

QString MemoryStore::identifyOrCreateSpeaker(const VoiceSignature &sig) {
    QSqlDatabase db = openDb(m_connectionName);
    if (!db.isValid() || !db.isOpen()) {
        return "speaker-unknown";
    }

    struct Candidate {
        int id = -1;
        QString label;
        double rms = 0.0;
        double zcr = 0.0;
        int count = 0;
        double score = 1e9;
    } best;

    QSqlQuery find(db);
    if (find.exec("SELECT id, label, rms, zcr, count FROM speaker_profiles")) {
        while (find.next()) {
            Candidate c;
            c.id = find.value(0).toInt();
            c.label = find.value(1).toString();
            c.rms = find.value(2).toDouble();
            c.zcr = find.value(3).toDouble();
            c.count = find.value(4).toInt();

            const double dr = c.rms - sig.rms;
            const double dz = c.zcr - sig.zcr;
            c.score = std::sqrt(dr * dr + dz * dz);

            if (c.score < best.score) {
                best = c;
            }
        }
    }

    const double threshold = 0.08;
    if (best.id >= 0 && best.score <= threshold) {
        const int newCount = best.count + 1;
        const double avgRms = ((best.rms * best.count) + sig.rms) / newCount;
        const double avgZcr = ((best.zcr * best.count) + sig.zcr) / newCount;

        QSqlQuery update(db);
        update.prepare("UPDATE speaker_profiles SET rms = ?, zcr = ?, count = ?, last_seen = ? WHERE id = ?");
        update.addBindValue(avgRms);
        update.addBindValue(avgZcr);
        update.addBindValue(newCount);
        update.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        update.addBindValue(best.id);
        update.exec();

        return best.label;
    }

    QSqlQuery countQuery(db);
    countQuery.exec("SELECT COUNT(*) FROM speaker_profiles");
    int n = 0;
    if (countQuery.next()) {
        n = countQuery.value(0).toInt();
    }

    const QString label = QString("speaker-%1").arg(n + 1);

    QSqlQuery insert(db);
    insert.prepare("INSERT INTO speaker_profiles(label, rms, zcr, count, last_seen) VALUES (?, ?, ?, 1, ?)");
    insert.addBindValue(label);
    insert.addBindValue(sig.rms);
    insert.addBindValue(sig.zcr);
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    insert.exec();

    return label;
}
