#pragma once

#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVector>

class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;

class GgWaveService : public QObject {
    Q_OBJECT

public:
    explicit GgWaveService(QObject *parent = nullptr);
    ~GgWaveService() override;

    static QStringList supportedProtocolKeys();
    static QString protocolLabel(const QString &key);
    QString transmitText(const QString &text,
                         int volume = 20,
                         const QString &protocolKey = "audible_fast",
                         int repeatCount = 1);

    bool startListening();
    void stopListening();
    bool isListening() const;

signals:
    void messageReceived(const QString &message);
    void statusChanged(const QString &message);

private slots:
    void onCaptureReadyRead();
    void onSinkStateChanged();

private:
    struct TxFrame {
        QByteArray payload;
        int volume = 20;
        int protocolId = 0;
    };

    struct RxAssembly {
        int totalParts = 0;
        QVector<QString> parts;
        QDateTime lastUpdate;
    };

    void startNextTxFrame();
    void handleDecodedMessage(const QString &decoded);
    void garbageCollectAssemblies();

    int m_txInstance = -1;
    int m_rxInstance = -1;

    QAudioSink *m_sink = nullptr;
    QBuffer *m_playBuffer = nullptr;
    QQueue<TxFrame> m_txQueue;
    bool m_txInProgress = false;

    QAudioSource *m_source = nullptr;
    QIODevice *m_captureDevice = nullptr;
    QByteArray m_rxPcmBuffer;
    QHash<QString, RxAssembly> m_rxAssemblies;
    QHash<QString, QDateTime> m_recentCompletedMessageIds;
};
