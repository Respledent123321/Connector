#pragma once

#include "ConfigManager.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct ToolManifest {
    QString name;
    QString description;
    QString command;
    QStringList arguments;
    bool enabled = true;
};

struct ToolRunResult {
    int exitCode = -1;
    QString output;
    QString error;
};

class ToolManager {
public:
    explicit ToolManager(QString toolsDir = QString());

    void setConfig(const AppConfig &config);

    QVector<ToolManifest> listTools() const;
    bool createTool(const ToolManifest &tool, QString *error = nullptr);
    bool deleteTool(const QString &name, QString *error = nullptr);

    ToolRunResult runTool(const QString &name, const QStringList &runtimeArgs = {}) const;

private:
    QString m_toolsDir;
    AppConfig m_config;

    QString toolPathForName(const QString &name) const;
    static QString sanitizeName(const QString &name);
    static ToolManifest fromJsonFile(const QString &path, bool *ok);
};
