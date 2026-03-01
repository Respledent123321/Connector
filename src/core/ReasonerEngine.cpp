#include "ReasonerEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {
QString clip(const QString &s, int maxLen) {
    if (s.size() <= maxLen) {
        return s;
    }
    return s.left(maxLen) + "...";
}

bool isLikelyBannerLine(const QString &line) {
    if (line.isEmpty()) {
        return false;
    }

    static const QRegularExpression blockChars("^[\\s\\x{2580}-\\x{259F}]+$");
    return blockChars.match(line).hasMatch();
}

bool isLikelyDiagnosticLine(const QString &trimmed) {
    if (trimmed.isEmpty()) {
        return false;
    }

    if (trimmed == "sampler params:" ||
        trimmed == "using custom system prompt" ||
        trimmed.startsWith("common_perf_print:") ||
        trimmed.startsWith("sampler seed:") ||
        trimmed.startsWith("sampler chain:") ||
        trimmed.startsWith("repeat_last_n") ||
        trimmed.startsWith("dry_multiplier") ||
        trimmed.startsWith("top_k") ||
        trimmed.startsWith("mirostat") ||
        trimmed.startsWith("generate:") ||
        trimmed.startsWith("system_info:") ||
        trimmed.startsWith("main:") ||
        trimmed.startsWith("common_init_result:") ||
        trimmed.startsWith("llama_params_fit") ||
        trimmed.startsWith("llama_model_loader:") ||
        trimmed.startsWith("llama_context:") ||
        trimmed.startsWith("llama_kv_cache:") ||
        trimmed.startsWith("print_info:") ||
        trimmed.startsWith("load_tensors:") ||
        trimmed.startsWith("load:") ||
        trimmed.startsWith("sched_reserve:")) {
        return true;
    }

    static const QRegularExpression sepDots("^\\.{8,}$");
    if (sepDots.match(trimmed).hasMatch()) {
        return true;
    }

    return false;
}

QString cleanLlamaOutput(const QString &raw) {
    QString text = raw;
    text.replace('\r', '\n');

    QStringList out;
    const QStringList lines = text.split('\n');
    bool skipCommandList = false;

    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty()) {
            if (!out.isEmpty() && !out.last().isEmpty()) {
                out << QString();
            }
            continue;
        }

        if (t == "Loading model..." ||
            t.startsWith("build") ||
            t.startsWith("model") ||
            t.startsWith("modalities") ||
            t.startsWith("load_backend:") ||
            t.startsWith("llama_memory_breakdown_print:") ||
            t.startsWith("[ Prompt:") ||
            t == "Exiting...") {
            continue;
        }

        if (t == "available commands:") {
            skipCommandList = true;
            continue;
        }
        if (skipCommandList) {
            if (t.startsWith('/')) {
                continue;
            }
            skipCommandList = false;
        }

        if (isLikelyBannerLine(t)) {
            continue;
        }
        if (isLikelyDiagnosticLine(t)) {
            continue;
        }

        if (t.startsWith("> ")) {
            continue;
        }

        out << line;
    }

    while (!out.isEmpty() && out.last().trimmed().isEmpty()) {
        out.removeLast();
    }

    QString cleaned = out.join('\n').trimmed();

    const int userMarker = cleaned.indexOf(QRegularExpression("\\buser\\s*:", QRegularExpression::CaseInsensitiveOption));
    if (userMarker >= 0) {
        cleaned = cleaned.left(userMarker).trimmed();
    }
    const int assistantMarker = cleaned.indexOf(QRegularExpression("\\bassistant\\s*:", QRegularExpression::CaseInsensitiveOption));
    if (assistantMarker >= 0) {
        cleaned = cleaned.left(assistantMarker).trimmed();
    }
    const int userMessageMarker = cleaned.indexOf(QRegularExpression("User message\\s*:", QRegularExpression::CaseInsensitiveOption));
    if (userMessageMarker >= 0) {
        cleaned = cleaned.left(userMessageMarker).trimmed();
    }
    const int recentMarker = cleaned.indexOf(QRegularExpression("Recent user messages\\s*:", QRegularExpression::CaseInsensitiveOption));
    if (recentMarker >= 0) {
        cleaned = cleaned.left(recentMarker).trimmed();
    }
    const int greetingsMarker = cleaned.indexOf(QRegularExpression("For my previous greetings\\s*:", QRegularExpression::CaseInsensitiveOption));
    if (greetingsMarker >= 0) {
        cleaned = cleaned.left(greetingsMarker).trimmed();
    }

    return cleaned;
}

QString normalizeSentence(const QString &s) {
    QString t = s.trimmed().toLower();
    t.replace(QRegularExpression("[^a-z0-9\\s]"), "");
    t.replace(QRegularExpression("\\s+"), " ");
    return t.trimmed();
}

QString dedupeAndTrimSentences(const QString &text, int maxSentences) {
    QString t = text.trimmed();
    if (t.isEmpty()) {
        return t;
    }

    QStringList parts = t.split(QRegularExpression("(?<=[.!?])\\s+"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return t;
    }

    QSet<QString> seen;
    QStringList kept;
    for (const QString &p : parts) {
        const QString n = normalizeSentence(p);
        if (n.isEmpty()) {
            continue;
        }
        if (seen.contains(n)) {
            break;
        }
        seen.insert(n);
        kept << p.trimmed();
        if (kept.size() >= maxSentences) {
            break;
        }
    }

    if (kept.isEmpty()) {
        return t;
    }

    return kept.join(" ").trimmed();
}

bool isSmallTalkPrompt(const QString &userText) {
    const QString t = userText.trimmed().toLower();
    return t.contains("how are you") ||
           t.contains("are you doing okay") ||
           t.contains("what's your name") ||
           t.contains("whats your name") ||
           t == "hi" || t == "hello" || t == "hey";
}
}

void ReasonerEngine::setConfig(const AppConfig &config) {
    m_config = config;
}

QString ReasonerEngine::runLlamaOnce(const QString &systemPrompt, const QString &userPrompt, int nPredict) const {
    QFileInfo cliInfo(m_config.llamaCliPath);
    QFileInfo completionInfo(QDir(cliInfo.absolutePath()).filePath("llama-completion.exe"));
    const bool useCli = cliInfo.exists();
    const QFileInfo exeInfo = useCli ? cliInfo : completionInfo;
    QFileInfo modelInfo(m_config.llamaModelPath);
    if (!exeInfo.exists() || !modelInfo.exists()) {
        return QString();
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setWorkingDirectory(exeInfo.absolutePath());
    QStringList args;
    args << "-m" << modelInfo.absoluteFilePath()
         << "-c" << "2048"
         << "-n" << QString::number(std::max(1, nPredict))
         << "--temp" << QString::number(std::max(0.0, m_config.llamaTemperature), 'f', 2)
         << "--repeat-penalty" << "1.15"
         << "--repeat-last-n" << "128"
         << "--presence-penalty" << "0.20"
         << "--frequency-penalty" << "0.20"
         << "--top-k" << "30"
         << "--top-p" << "0.90"
         << "--verbosity" << "1"
         << "--no-perf"
         << "--simple-io"
         << "--no-warmup"
         << "--no-display-prompt";

    if (useCli) {
        args << "-cnv"
             << "-st"
             << "-sys" << systemPrompt
             << "-p" << userPrompt;
    } else {
        const QString mergedPrompt = "System:\n" + systemPrompt + "\n\nUser:\n" + userPrompt + "\n\nAssistant:";
        args << "--no-conversation"
             << "--single-turn"
             << "-r" << "User:"
             << "-p" << mergedPrompt;
    }

    if (m_config.llamaThreads > 0) {
        args << "-t" << QString::number(m_config.llamaThreads);
    }

    process.start(exeInfo.absoluteFilePath(), args);
    if (!process.waitForStarted(3000)) {
        return QString();
    }

    const int timeoutMs = std::clamp(15000 + (nPredict * 500), 45000, 180000);
    if (!process.waitForFinished(timeoutMs)) {
        const QString partial = cleanLlamaOutput(QString::fromUtf8(process.readAll()));
        process.terminate();
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(5000);
        }
        if (!partial.isEmpty()) {
            return partial;
        }
        return QString();
    }

    const QString stdoutText = cleanLlamaOutput(QString::fromUtf8(process.readAll()));
    if (stdoutText.isEmpty()) {
        return QString();
    }

    return stdoutText;
}

QString ReasonerEngine::generateWithLlamaCli(const QString &prompt) const {
    const int segmentPredict = std::clamp(m_config.llamaNPredict, 64, 128);
    return runLlamaOnce(
               "You are a local autonomous robot assistant focused on reasoning, learning, research, and tool-use planning. "
               "Use concise answers, but be accurate and structured when needed. "
               "Do not mention internal model settings, logs, or debug tips unless the user explicitly asks for troubleshooting. "
               "For greetings or wellbeing questions, reply naturally in 1-2 sentences. "
               "When uncertain, state uncertainty and propose a verification step.",
               prompt,
               segmentPredict)
        .trimmed();
}

QString ReasonerEngine::generateReply(const QString &userText,
                                      const QVector<ConversationEntry> &recent,
                                      const QVector<MemoryEntry> &memories,
                                      const QString &researchSummary) const {
    const QString userTextTrimmed = userText.trimmed();
    QString prompt;
    prompt += "Current UTC time: " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + "\n";
    prompt += "User message: " + userText + "\n";

    if (!researchSummary.isEmpty()) {
        prompt += "Research notes: " + researchSummary + "\n";
    }

    if (!memories.isEmpty()) {
        prompt += "Known memories:\n";
        for (const MemoryEntry &m : memories) {
            prompt += "- [" + m.category + "] " + clip(m.content, 160) + "\n";
        }
    }

    if (!recent.isEmpty()) {
        QStringList priorUserTurns;
        bool skippedCurrentUserEcho = false;
        for (int i = recent.size() - 1; i >= 0; --i) {
            const ConversationEntry &c = recent[i];
            if (c.role.compare("user", Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString turn = c.text.trimmed();
            if (!skippedCurrentUserEcho && turn.compare(userTextTrimmed, Qt::CaseInsensitive) == 0) {
                skippedCurrentUserEcho = true;
                continue;
            }
            if (!turn.isEmpty()) {
                priorUserTurns.prepend(clip(turn, 140));
            }
            if (priorUserTurns.size() >= 2) {
                break;
            }
        }

        if (!priorUserTurns.isEmpty()) {
            prompt += "Recent user messages:\n";
            for (const QString &turn : priorUserTurns) {
                prompt += "- " + turn + "\n";
            }
        }
    }

    const QString llm = generateWithLlamaCli(prompt);
    if (!llm.isEmpty()) {
        if (isSmallTalkPrompt(userTextTrimmed)) {
            return dedupeAndTrimSentences(llm, 2);
        }
        return dedupeAndTrimSentences(llm, 6);
    }

    QFileInfo exeInfo(m_config.llamaCliPath);
    QFileInfo modelInfo(m_config.llamaModelPath);

    QString fallback;
    if (!exeInfo.exists() || !modelInfo.exists()) {
        fallback = "Local model files are missing. Check Settings paths and run Install/Upgrade Local Models. ";
    } else {
        fallback = "Local model call timed out or returned no output. Try lowering llama_n_predict to 64-128. ";
    }

    if (!researchSummary.isEmpty()) {
        fallback += "I found this while researching: " + clip(researchSummary, 240) + " ";
    }
    fallback += "You said: \"" + clip(userText, 200) + "\". ";

    if (!memories.isEmpty()) {
        fallback += "I remember " + QString::number(memories.size()) + " recent memory items.";
    } else {
        fallback += "You can store memories with /remember <text>.";
    }

    return fallback;
}
