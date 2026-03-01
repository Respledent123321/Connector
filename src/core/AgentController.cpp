#include "AgentController.h"

#include <QDateTime>
#include <QUuid>

AgentController::AgentController(MemoryStore *memoryStore,
                                 ReasonerEngine *reasoner,
                                 ResearchService *research,
                                 ToolManager *tools,
                                 QObject *parent)
    : QObject(parent),
      m_memory(memoryStore),
      m_reasoner(reasoner),
      m_research(research),
      m_tools(tools),
      m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

QString AgentController::sessionId() const {
    return m_sessionId;
}

void AgentController::setResearchEnabled(bool enabled) {
    m_researchEnabled = enabled;
}

QString AgentController::handleUserMessage(const QString &text, const QString &speaker) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    m_memory->addConversation("user", trimmed, speaker, m_sessionId);

    QString reply;

    if (trimmed.startsWith("/remember ", Qt::CaseInsensitive)) {
        const QString body = trimmed.mid(QString("/remember ").size()).trimmed();
        if (body.isEmpty()) {
            reply = "Usage: /remember <text>";
        } else if (m_memory->addMemory("note", body)) {
            reply = "Saved to long-term memory.";
        } else {
            reply = "Failed to save memory.";
        }
    } else if (trimmed.startsWith("/forget ", Qt::CaseInsensitive)) {
        bool ok = false;
        const int id = trimmed.mid(QString("/forget ").size()).trimmed().toInt(&ok);
        if (!ok) {
            reply = "Usage: /forget <memory_id>";
        } else if (m_memory->forgetMemory(id)) {
            reply = "Memory deleted.";
        } else {
            reply = "Could not delete memory.";
        }
    } else if (trimmed.startsWith("/research ", Qt::CaseInsensitive)) {
        if (!m_researchEnabled) {
            reply = "Research is disabled in settings.";
            m_memory->addConversation("assistant", reply, "robot", m_sessionId);
            emit responseReady(reply);
            return reply;
        }

        const QString query = trimmed.mid(QString("/research ").size()).trimmed();
        if (query.isEmpty()) {
            reply = "Usage: /research <query>";
        } else {
            const ResearchResult result = m_research->search(query);
            if (!result.error.isEmpty()) {
                reply = "Research error: " + result.error;
            } else {
                reply = "Research summary: " + result.summary;
                if (!result.sources.isEmpty()) {
                    reply += "\nSources:";
                    for (const QString &s : result.sources) {
                        reply += "\n- " + s;
                    }
                }
            }
        }
    } else if (trimmed.startsWith("/tool run ", Qt::CaseInsensitive)) {
        const QString name = trimmed.mid(QString("/tool run ").size()).trimmed();
        if (name.isEmpty()) {
            reply = "Usage: /tool run <tool_name>";
        } else {
            const ToolRunResult run = m_tools->runTool(name);
            if (!run.error.isEmpty()) {
                reply = "Tool run failed: " + run.error;
            } else {
                reply = "Tool exit code: " + QString::number(run.exitCode) + "\n" + run.output;
            }
        }
    } else {
        const QVector<ConversationEntry> recent = m_memory->recentConversations(10);
        const QVector<MemoryEntry> memories = m_memory->listMemories(8);

        reply = m_reasoner->generateReply(trimmed, recent, memories, QString());
    }

    if (reply.isEmpty()) {
        reply = "I could not generate a response.";
    }

    m_memory->addConversation("assistant", reply, "robot", m_sessionId);
    emit responseReady(reply);
    return reply;
}
