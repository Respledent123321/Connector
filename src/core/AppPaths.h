#pragma once

#include <QString>

class AppPaths {
public:
    static QString rootDir();
    static QString configPath();
    static QString dataDir();
    static QString toolsDir();
};
