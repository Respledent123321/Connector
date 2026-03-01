#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString AppPaths::rootDir() {
    const QString envRoot = qEnvironmentVariable("ROBOT_HOME");
    if (!envRoot.isEmpty()) {
        return QDir(envRoot).absolutePath();
    }

    QDir appDir(QCoreApplication::applicationDirPath());

    QDir probe = appDir;
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(probe.filePath("config/robot.yaml"))) {
            return probe.absolutePath();
        }
        if (!probe.cdUp()) {
            break;
        }
    }

    return appDir.absolutePath();
}

QString AppPaths::configPath() {
    return QDir(rootDir()).filePath("config/robot.yaml");
}

QString AppPaths::dataDir() {
    return QDir(rootDir()).filePath("data");
}

QString AppPaths::toolsDir() {
    return QDir(dataDir()).filePath("tools");
}
