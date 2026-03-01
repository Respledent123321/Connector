#pragma once

#include "MemoryStore.h"
#include "ReasonerEngine.h"
#include "ResearchService.h"
#include "ToolManager.h"

#include <QObject>

class AgentController : public QObject {
    Q_OBJECT

public:
    AgentController(MemoryStore *memoryStore,
                    ReasonerEngine *reasoner,
                    ResearchService *research,
                    ToolManager *tools,
                    QObject *parent = nullptr);

    QString sessionId() const;
    void setResearchEnabled(bool enabled);

    QString handleUserMessage(const QString &text, const QString &speaker);

signals:
    void responseReady(const QString &response);

private:
    MemoryStore *m_memory = nullptr;
    ReasonerEngine *m_reasoner = nullptr;
    ResearchService *m_research = nullptr;
    ToolManager *m_tools = nullptr;
    bool m_researchEnabled = true;

    QString m_sessionId;
};
