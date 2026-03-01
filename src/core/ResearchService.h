#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

struct ResearchResult {
    QString summary;
    QStringList sources;
    QString error;
};

class ResearchService : public QObject {
    Q_OBJECT

public:
    explicit ResearchService(QObject *parent = nullptr);

    ResearchResult search(const QString &query) const;
};
