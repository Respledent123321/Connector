#include "ToolManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <utility>

ToolManager::ToolManager(QString toolsDir)
    : m_toolsDir(std::move(toolsDir)) {}

void ToolManager::setConfig(const AppConfig &config) {
    m_config = config;
}

QString ToolManager::sanitizeName(const QString &name) {
    QString out;
    out.reserve(name.size());
    for (QChar c : name) {
        if (c.isLetterOrNumber() || c == '_' || c == '-') {
            out.append(c);
        }
    }
    return out;
}

QString ToolManager::toolPathForName(const QString &name) const {
    return QDir(m_toolsDir).filePath(sanitizeName(name) + ".json");
}

ToolManifest ToolManager::fromJsonFile(const QString &path, bool *ok) {
    ToolManifest t;
    *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return t;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return t;
    }

    const QJsonObject o = doc.object();
    t.name = o.value("name").toString();
    t.description = o.value("description").toString();
    t.command = o.value("command").toString();
    t.enabled = o.value("enabled").toBool(true);

    const QJsonArray args = o.value("arguments").toArray();
    for (const QJsonValue &v : args) {
        t.arguments << v.toString();
    }

    *ok = !t.name.isEmpty() && !t.command.isEmpty();
    return t;
}

QVector<ToolManifest> ToolManager::listTools() const {
    QVector<ToolManifest> out;
    QDir dir(m_toolsDir);
    if (!dir.exists()) {
        return out;
    }

    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        bool ok = false;
        ToolManifest t = fromJsonFile(fi.absoluteFilePath(), &ok);
        if (ok) {
            out.push_back(t);
        }
    }

    return out;
}

bool ToolManager::createTool(const ToolManifest &tool, QString *error) {
    const QString safe = sanitizeName(tool.name);
    if (safe.isEmpty()) {
        if (error) {
            *error = "Tool name is invalid.";
        }
        return false;
    }

    QDir dir(m_toolsDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        if (error) {
            *error = "Failed to create tools directory.";
        }
        return false;
    }

    QJsonObject o;
    o.insert("name", tool.name);
    o.insert("description", tool.description);
    o.insert("command", tool.command);
    o.insert("enabled", tool.enabled);

    QJsonArray args;
    for (const QString &a : tool.arguments) {
        args.append(a);
    }
    o.insert("arguments", args);

    QFile file(toolPathForName(tool.name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = "Failed to write tool file.";
        }
        return false;
    }

    file.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool ToolManager::deleteTool(const QString &name, QString *error) {
    QFile file(toolPathForName(name));
    if (!file.exists()) {
        if (error) {
            *error = "Tool does not exist.";
        }
        return false;
    }

    if (!file.remove()) {
        if (error) {
            *error = "Failed to delete tool file.";
        }
        return false;
    }

    return true;
}

ToolRunResult ToolManager::runTool(const QString &name, const QStringList &runtimeArgs) const {
    ToolRunResult out;

    bool ok = false;
    ToolManifest tool = fromJsonFile(toolPathForName(name), &ok);
    if (!ok) {
        out.error = "Tool not found or invalid.";
        return out;
    }

    if (!tool.enabled) {
        out.error = "Tool is disabled.";
        return out;
    }

    const QString programBase = QFileInfo(tool.command).baseName().toLower();
    bool allowed = m_config.toolAllowlist.isEmpty();
    for (const QString &allow : m_config.toolAllowlist) {
        if (allow.trimmed().toLower() == programBase) {
            allowed = true;
            break;
        }
    }

    if (!allowed) {
        out.error = "Tool command is not allowlisted.";
        return out;
    }

    QProcess proc;
    proc.setProgram(tool.command);

    QStringList args = tool.arguments;
    args.append(runtimeArgs);
    proc.setArguments(args);

    if (m_config.readOnlyWorkspace) {
        proc.setWorkingDirectory(m_toolsDir);
    }

    proc.start();
    if (!proc.waitForStarted(3000)) {
        out.error = "Failed to start tool process.";
        return out;
    }

    if (!proc.waitForFinished(60000)) {
        proc.kill();
        out.error = "Tool process timed out.";
        return out;
    }

    out.exitCode = proc.exitCode();
    out.output = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(proc.readAllStandardError());
    if (!stderrText.isEmpty()) {
        if (!out.output.isEmpty()) {
            out.output += "\n";
        }
        out.output += stderrText;
    }

    return out;
}
