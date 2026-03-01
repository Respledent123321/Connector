#include "ConfigManager.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>

namespace {
bool toBool(const QString &v, bool fallback) {
    const QString n = v.trimmed().toLower();
    if (n == "true" || n == "1" || n == "yes" || n == "on") {
        return true;
    }
    if (n == "false" || n == "0" || n == "no" || n == "off") {
        return false;
    }
    return fallback;
}

int toInt(const QString &v, int fallback) {
    bool ok = false;
    const int parsed = v.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

double toDouble(const QString &v, double fallback) {
    bool ok = false;
    const double parsed = v.trimmed().toDouble(&ok);
    return ok ? parsed : fallback;
}
}

QString ConfigManager::parseScalar(const QString &line) {
    const int colon = line.indexOf(':');
    if (colon < 0) {
        return QString();
    }
    return line.mid(colon + 1).trimmed();
}

QStringList ConfigManager::parseList(const QString &value) {
    QString v = value.trimmed();
    if (v.startsWith('[') && v.endsWith(']')) {
        v = v.mid(1, v.size() - 2);
    }

    QStringList out;
    const QStringList parts = v.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString item = part.trimmed();
        if ((item.startsWith('"') && item.endsWith('"')) || (item.startsWith('\'') && item.endsWith('\''))) {
            item = item.mid(1, item.size() - 2);
        }
        if (!item.isEmpty()) {
            out << item;
        }
    }
    return out;
}

bool ConfigManager::load(const QString &path) {
    QFile file(path);
    if (!file.exists()) {
        m_config.toolAllowlist = {"python", "powershell", "cmd", "git"};
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        const QString line = rawLine.trimmed();

        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        if (line.startsWith("llama_cli_path:")) {
            m_config.llamaCliPath = parseScalar(line);
        } else if (line.startsWith("llama_model_path:")) {
            m_config.llamaModelPath = parseScalar(line);
        } else if (line.startsWith("llm_profile:")) {
            const QString profile = parseScalar(line).trimmed().toLower();
            if (profile == "fast" || profile == "balanced" || profile == "strong") {
                m_config.llmProfile = profile;
            }
        } else if (line.startsWith("whisper_cli_path:")) {
            m_config.whisperCliPath = parseScalar(line);
        } else if (line.startsWith("whisper_model_path:")) {
            m_config.whisperModelPath = parseScalar(line);
        } else if (line.startsWith("piper_path:")) {
            m_config.piperPath = parseScalar(line);
        } else if (line.startsWith("piper_model_path:")) {
            m_config.piperModelPath = parseScalar(line);
        } else if (line.startsWith("piper_config_path:")) {
            m_config.piperConfigPath = parseScalar(line);
        } else if (line.startsWith("llama_n_predict:")) {
            m_config.llamaNPredict = toInt(parseScalar(line), 128);
        } else if (line.startsWith("llama_temperature:")) {
            m_config.llamaTemperature = toDouble(parseScalar(line), 0.4);
        } else if (line.startsWith("llama_threads:")) {
            m_config.llamaThreads = toInt(parseScalar(line), 0);
        } else if (line.startsWith("research_enabled:")) {
            m_config.researchEnabled = toBool(parseScalar(line), true);
        } else if (line.startsWith("speak_responses:")) {
            m_config.speakResponses = toBool(parseScalar(line), false);
        } else if (line.startsWith("read_only_workspace:")) {
            m_config.readOnlyWorkspace = toBool(parseScalar(line), true);
        } else if (line.startsWith("ggwave_tx_volume:")) {
            m_config.ggwaveTxVolume = toInt(parseScalar(line), 20);
        } else if (line.startsWith("ggwave_protocol:")) {
            m_config.ggwaveProtocol = parseScalar(line);
        } else if (line.startsWith("ggwave_repeat_count:")) {
            m_config.ggwaveRepeatCount = toInt(parseScalar(line), 1);
        } else if (line.startsWith("ggwave_route_to_chat:")) {
            m_config.ggwaveRouteToChat = toBool(parseScalar(line), false);
        } else if (line.startsWith("tool_allowlist:")) {
            m_config.toolAllowlist = parseList(parseScalar(line));
        }
    }

    if (m_config.toolAllowlist.isEmpty()) {
        m_config.toolAllowlist = {"python", "powershell", "cmd", "git"};
    }

    return true;
}

bool ConfigManager::save(const QString &path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Robot GUI config\n";
    stream << "llama_cli_path: " << m_config.llamaCliPath << "\n";
    stream << "llama_model_path: " << m_config.llamaModelPath << "\n";
    stream << "llm_profile: " << m_config.llmProfile << "\n";
    stream << "whisper_cli_path: " << m_config.whisperCliPath << "\n";
    stream << "whisper_model_path: " << m_config.whisperModelPath << "\n";
    stream << "piper_path: " << m_config.piperPath << "\n";
    stream << "piper_model_path: " << m_config.piperModelPath << "\n";
    stream << "piper_config_path: " << m_config.piperConfigPath << "\n";
    stream << "llama_n_predict: " << std::clamp(m_config.llamaNPredict, 64, 512) << "\n";
    stream << "llama_temperature: " << m_config.llamaTemperature << "\n";
    stream << "llama_threads: " << m_config.llamaThreads << "\n";
    stream << "research_enabled: " << (m_config.researchEnabled ? "true" : "false") << "\n";
    stream << "speak_responses: " << (m_config.speakResponses ? "true" : "false") << "\n";
    stream << "read_only_workspace: " << (m_config.readOnlyWorkspace ? "true" : "false") << "\n";
    stream << "ggwave_tx_volume: " << m_config.ggwaveTxVolume << "\n";
    stream << "ggwave_protocol: " << m_config.ggwaveProtocol << "\n";
    stream << "ggwave_repeat_count: " << m_config.ggwaveRepeatCount << "\n";
    stream << "ggwave_route_to_chat: " << (m_config.ggwaveRouteToChat ? "true" : "false") << "\n";

    stream << "tool_allowlist: [";
    for (int i = 0; i < m_config.toolAllowlist.size(); ++i) {
        stream << m_config.toolAllowlist[i];
        if (i + 1 < m_config.toolAllowlist.size()) {
            stream << ", ";
        }
    }
    stream << "]\n";

    return true;
}

const AppConfig &ConfigManager::config() const {
    return m_config;
}

AppConfig &ConfigManager::config() {
    return m_config;
}
