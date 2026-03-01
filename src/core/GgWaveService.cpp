#include "GgWaveService.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QHash>
#include <QIODevice>
#include <QSignalBlocker>
#include <QUuid>
#include <QtGlobal>

#include "ggwave/ggwave.h"

namespace {
QHash<QString, ggwave_ProtocolId> protocolMap() {
    QHash<QString, ggwave_ProtocolId> map;
    map.insert("audible_normal", GGWAVE_PROTOCOL_AUDIBLE_NORMAL);
    map.insert("audible_fast", GGWAVE_PROTOCOL_AUDIBLE_FAST);
    map.insert("audible_fastest", GGWAVE_PROTOCOL_AUDIBLE_FASTEST);
    map.insert("ultrasound_normal", GGWAVE_PROTOCOL_ULTRASOUND_NORMAL);
    map.insert("ultrasound_fast", GGWAVE_PROTOCOL_ULTRASOUND_FAST);
    map.insert("ultrasound_fastest", GGWAVE_PROTOCOL_ULTRASOUND_FASTEST);
    map.insert("dt_normal", GGWAVE_PROTOCOL_DT_NORMAL);
    map.insert("dt_fast", GGWAVE_PROTOCOL_DT_FAST);
    map.insert("dt_fastest", GGWAVE_PROTOCOL_DT_FASTEST);
    map.insert("mt_normal", GGWAVE_PROTOCOL_MT_NORMAL);
    map.insert("mt_fast", GGWAVE_PROTOCOL_MT_FAST);
    map.insert("mt_fastest", GGWAVE_PROTOCOL_MT_FASTEST);
    return map;
}

QStringList splitByCharCount(const QString &text, int maxCharsPerChunk) {
    QStringList chunks;
    if (text.isEmpty()) {
        return chunks;
    }

    int offset = 0;
    while (offset < text.size()) {
        const int n = qMin(maxCharsPerChunk, text.size() - offset);
        chunks << text.mid(offset, n);
        offset += n;
    }
    return chunks;
}

constexpr auto kPacketPrefix = "RBT1|";
}

QStringList GgWaveService::supportedProtocolKeys() {
    return {
        "audible_normal",
        "audible_fast",
        "audible_fastest",
        "ultrasound_normal",
        "ultrasound_fast",
        "ultrasound_fastest",
        "dt_normal",
        "dt_fast",
        "dt_fastest",
        "mt_normal",
        "mt_fast",
        "mt_fastest",
    };
}

QString GgWaveService::protocolLabel(const QString &key) {
    if (key == "audible_normal") return "Audible Normal";
    if (key == "audible_fast") return "Audible Fast";
    if (key == "audible_fastest") return "Audible Fastest";
    if (key == "ultrasound_normal") return "Ultrasound Normal";
    if (key == "ultrasound_fast") return "Ultrasound Fast";
    if (key == "ultrasound_fastest") return "Ultrasound Fastest";
    if (key == "dt_normal") return "DT Normal";
    if (key == "dt_fast") return "DT Fast";
    if (key == "dt_fastest") return "DT Fastest";
    if (key == "mt_normal") return "MT Normal";
    if (key == "mt_fast") return "MT Fast";
    if (key == "mt_fastest") return "MT Fastest";
    return key;
}

GgWaveService::GgWaveService(QObject *parent)
    : QObject(parent) {
    ggwave_Parameters params = ggwave_getDefaultParameters();
    params.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    params.sampleFormatOut = GGWAVE_SAMPLE_FORMAT_I16;
    params.sampleRateInp = 48000;
    params.sampleRateOut = 48000;
    params.sampleRate = 48000;
    params.samplesPerFrame = 1024;
    params.operatingMode = GGWAVE_OPERATING_MODE_RX_AND_TX;

    m_txInstance = ggwave_init(params);
    m_rxInstance = ggwave_init(params);

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    m_sink = new QAudioSink(format, this);
    m_source = new QAudioSource(format, this);
    connect(m_sink, &QAudioSink::stateChanged, this, &GgWaveService::onSinkStateChanged);
}

GgWaveService::~GgWaveService() {
    stopListening();

    if (m_sink) {
        m_sink->stop();
    }

    if (m_txInstance >= 0) {
        ggwave_free(m_txInstance);
        m_txInstance = -1;
    }

    if (m_rxInstance >= 0) {
        ggwave_free(m_rxInstance);
        m_rxInstance = -1;
    }
}

QString GgWaveService::transmitText(const QString &text, int volume, const QString &protocolKey, int repeatCount) {
    if (m_txInstance < 0) {
        return "ggwave TX is not initialized.";
    }

    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return "Nothing to transmit.";
    }

    const auto protocols = protocolMap();
    const ggwave_ProtocolId protocolId = protocols.value(protocolKey, GGWAVE_PROTOCOL_AUDIBLE_FAST);
    const int clampedVolume = qBound(1, volume, 100);
    const int clampedRepeat = qBound(1, repeatCount, 10);
    const QStringList chunks = splitByCharCount(normalized, 70);
    const QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    for (int i = 0; i < chunks.size(); ++i) {
        const QString packet = QString("%1%2|%3|%4|%5")
                                   .arg(kPacketPrefix)
                                   .arg(msgId)
                                   .arg(i + 1)
                                   .arg(chunks.size())
                                   .arg(chunks[i]);
        TxFrame frame;
        frame.payload = packet.toUtf8();
        frame.volume = clampedVolume;
        frame.protocolId = protocolId;

        for (int r = 0; r < clampedRepeat; ++r) {
            m_txQueue.enqueue(frame);
        }
    }

    if (!m_txInProgress) {
        startNextTxFrame();
    }

    return QString("Queued %1 chunk(s), %2 frame(s).")
        .arg(chunks.size())
        .arg(chunks.size() * clampedRepeat);
}

bool GgWaveService::startListening() {
    if (!m_source) {
        return false;
    }

    if (m_captureDevice) {
        return true;
    }

    m_rxPcmBuffer.clear();
    m_captureDevice = m_source->start();
    if (!m_captureDevice) {
        return false;
    }

    connect(m_captureDevice, &QIODevice::readyRead, this, &GgWaveService::onCaptureReadyRead);
    emit statusChanged("ggwave listening enabled.");
    return true;
}

void GgWaveService::stopListening() {
    if (!m_source) {
        return;
    }

    if (m_captureDevice) {
        disconnect(m_captureDevice, &QIODevice::readyRead, this, &GgWaveService::onCaptureReadyRead);
        m_source->stop();
        m_captureDevice = nullptr;
    }

    m_rxPcmBuffer.clear();
    emit statusChanged("ggwave listening disabled.");
}

bool GgWaveService::isListening() const {
    return m_captureDevice != nullptr;
}

void GgWaveService::onCaptureReadyRead() {
    if (!m_captureDevice || m_rxInstance < 0) {
        return;
    }

    const QByteArray chunk = m_captureDevice->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    m_rxPcmBuffer.append(chunk);

    const int frameBytes = 1024 * static_cast<int>(sizeof(int16_t));
    while (m_rxPcmBuffer.size() >= frameBytes) {
        QByteArray frame = m_rxPcmBuffer.left(frameBytes);
        m_rxPcmBuffer.remove(0, frameBytes);

        char decoded[256] = {0};
        const int n = ggwave_decode(m_rxInstance, frame.constData(), frame.size(), decoded);
        if (n > 0) {
            handleDecodedMessage(QString::fromUtf8(decoded, n));
        }
    }
}

void GgWaveService::onSinkStateChanged() {
    if (!m_txInProgress) {
        return;
    }

    if (m_sink->state() == QAudio::IdleState || m_sink->state() == QAudio::StoppedState) {
        startNextTxFrame();
    }
}

void GgWaveService::startNextTxFrame() {
    if (m_playBuffer) {
        m_playBuffer->close();
        m_playBuffer->deleteLater();
        m_playBuffer = nullptr;
    }

    if (m_txQueue.isEmpty()) {
        m_txInProgress = false;
        emit statusChanged("Transmission complete.");
        return;
    }

    const TxFrame frame = m_txQueue.dequeue();
    const int bytesNeeded = ggwave_encode(
        m_txInstance,
        frame.payload.constData(),
        frame.payload.size(),
        static_cast<ggwave_ProtocolId>(frame.protocolId),
        frame.volume,
        nullptr,
        1);

    if (bytesNeeded <= 0) {
        emit statusChanged("Failed to encode a packet frame.");
        m_txInProgress = false;
        m_txQueue.clear();
        return;
    }

    QByteArray waveform;
    waveform.resize(bytesNeeded);

    const int encodedBytes = ggwave_encode(
        m_txInstance,
        frame.payload.constData(),
        frame.payload.size(),
        static_cast<ggwave_ProtocolId>(frame.protocolId),
        frame.volume,
        waveform.data(),
        0);

    if (encodedBytes <= 0) {
        emit statusChanged("Packet frame encoding failed.");
        m_txInProgress = false;
        m_txQueue.clear();
        return;
    }

    m_playBuffer = new QBuffer(this);
    m_playBuffer->setData(waveform);
    m_playBuffer->open(QIODevice::ReadOnly);

    m_txInProgress = true;
    {
        // Prevent re-entrant stateChanged(Stopped) recursion while switching buffers.
        const QSignalBlocker blocker(m_sink);
        m_sink->stop();
        m_sink->start(m_playBuffer);
    }
}

void GgWaveService::handleDecodedMessage(const QString &decoded) {
    garbageCollectAssemblies();

    if (!decoded.startsWith(kPacketPrefix)) {
        emit messageReceived(decoded);
        return;
    }

    const int p1 = decoded.indexOf('|');
    const int p2 = decoded.indexOf('|', p1 + 1);
    const int p3 = decoded.indexOf('|', p2 + 1);
    const int p4 = decoded.indexOf('|', p3 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) {
        emit messageReceived(decoded);
        return;
    }

    const QString msgId = decoded.mid(p1 + 1, p2 - p1 - 1);
    bool idxOk = false;
    bool totalOk = false;
    const int idx = decoded.mid(p2 + 1, p3 - p2 - 1).toInt(&idxOk);
    const int total = decoded.mid(p3 + 1, p4 - p3 - 1).toInt(&totalOk);
    const QString body = decoded.mid(p4 + 1);

    if (!idxOk || !totalOk || idx <= 0 || total <= 0 || idx > total || msgId.isEmpty()) {
        emit messageReceived(decoded);
        return;
    }

    if (m_recentCompletedMessageIds.contains(msgId)) {
        // This message was already fully reconstructed; ignore repeated frames.
        return;
    }

    RxAssembly &assembly = m_rxAssemblies[msgId];
    if (assembly.totalParts != total) {
        assembly.totalParts = total;
        assembly.parts = QVector<QString>(total);
    }
    assembly.lastUpdate = QDateTime::currentDateTimeUtc();

    if (idx - 1 < assembly.parts.size() && assembly.parts[idx - 1].isEmpty()) {
        assembly.parts[idx - 1] = body;
    }

    bool complete = true;
    for (const QString &p : assembly.parts) {
        if (p.isEmpty()) {
            complete = false;
            break;
        }
    }

    if (!complete) {
        return;
    }

    QString joined;
    joined.reserve(assembly.parts.size() * 64);
    for (const QString &p : assembly.parts) {
        joined += p;
    }

    m_rxAssemblies.remove(msgId);
    m_recentCompletedMessageIds.insert(msgId, QDateTime::currentDateTimeUtc());
    emit messageReceived(joined);
}

void GgWaveService::garbageCollectAssemblies() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<QString> stale;
    for (auto it = m_rxAssemblies.constBegin(); it != m_rxAssemblies.constEnd(); ++it) {
        if (it.value().lastUpdate.msecsTo(now) > 30000) {
            stale << it.key();
        }
    }
    for (const QString &k : stale) {
        m_rxAssemblies.remove(k);
    }

    QList<QString> oldCompleted;
    for (auto it = m_recentCompletedMessageIds.constBegin(); it != m_recentCompletedMessageIds.constEnd(); ++it) {
        if (it.value().msecsTo(now) > 30000) {
            oldCompleted << it.key();
        }
    }
    for (const QString &k : oldCompleted) {
        m_recentCompletedMessageIds.remove(k);
    }
}
