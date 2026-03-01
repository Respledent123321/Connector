#include "ResearchService.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

ResearchService::ResearchService(QObject *parent)
    : QObject(parent) {}

ResearchResult ResearchService::search(const QString &query) const {
    ResearchResult out;

    if (query.trimmed().isEmpty()) {
        out.error = "Query is empty.";
        return out;
    }

    QNetworkAccessManager manager;

    QUrl url("https://api.duckduckgo.com/");
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("format", "json");
    q.addQueryItem("no_html", "1");
    q.addQueryItem("skip_disambig", "1");
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "RobotGUI/1.0");

    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timeout.start(10000);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        out.error = "Research request timed out.";
        reply->deleteLater();
        return out;
    }

    if (reply->error() != QNetworkReply::NoError) {
        out.error = reply->errorString();
        reply->deleteLater();
        return out;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = "Invalid research response format.";
        return out;
    }

    const QJsonObject obj = doc.object();
    const QString heading = obj.value("Heading").toString();
    const QString abstractText = obj.value("AbstractText").toString();
    const QString abstractUrl = obj.value("AbstractURL").toString();

    if (!abstractText.isEmpty()) {
        out.summary = heading.isEmpty() ? abstractText : (heading + ": " + abstractText);
    }

    if (!abstractUrl.isEmpty()) {
        out.sources << abstractUrl;
    }

    const QJsonArray related = obj.value("RelatedTopics").toArray();
    for (const QJsonValue &v : related) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject ro = v.toObject();

        if (ro.contains("FirstURL")) {
            const QString link = ro.value("FirstURL").toString();
            if (!link.isEmpty() && !out.sources.contains(link)) {
                out.sources << link;
            }

            if (out.summary.isEmpty()) {
                out.summary = ro.value("Text").toString();
            }
        } else if (ro.contains("Topics")) {
            const QJsonArray topics = ro.value("Topics").toArray();
            for (const QJsonValue &topicVal : topics) {
                if (!topicVal.isObject()) {
                    continue;
                }
                const QJsonObject to = topicVal.toObject();
                const QString link = to.value("FirstURL").toString();
                if (!link.isEmpty() && !out.sources.contains(link)) {
                    out.sources << link;
                }
                if (out.summary.isEmpty()) {
                    out.summary = to.value("Text").toString();
                }
                if (out.sources.size() >= 5) {
                    break;
                }
            }
        }

        if (out.sources.size() >= 5) {
            break;
        }
    }

    if (out.summary.isEmpty()) {
        out.summary = "No concise result found for: " + query;
    }

    return out;
}
