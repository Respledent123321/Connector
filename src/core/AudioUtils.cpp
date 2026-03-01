#include "AudioUtils.h"

#include <QFile>
#include <QtEndian>

#include <cmath>

namespace {
void appendU32(QByteArray &dst, quint32 value) {
    quint32 le = qToLittleEndian(value);
    dst.append(reinterpret_cast<const char *>(&le), sizeof(le));
}

void appendU16(QByteArray &dst, quint16 value) {
    quint16 le = qToLittleEndian(value);
    dst.append(reinterpret_cast<const char *>(&le), sizeof(le));
}
}

bool AudioUtils::writePcm16Wav(const QString &path, const QByteArray &pcm16Mono, int sampleRate) {
    const quint16 channels = 1;
    const quint16 bitsPerSample = 16;
    const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
    const quint16 blockAlign = channels * (bitsPerSample / 8);
    const quint32 dataSize = static_cast<quint32>(pcm16Mono.size());
    const quint32 riffSize = 36 + dataSize;

    QByteArray header;
    header.reserve(44);

    header.append("RIFF", 4);
    appendU32(header, riffSize);
    header.append("WAVE", 4);

    header.append("fmt ", 4);
    appendU32(header, 16);
    appendU16(header, 1);
    appendU16(header, channels);
    appendU32(header, static_cast<quint32>(sampleRate));
    appendU32(header, byteRate);
    appendU16(header, blockAlign);
    appendU16(header, bitsPerSample);

    header.append("data", 4);
    appendU32(header, dataSize);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    if (file.write(header) != header.size()) {
        return false;
    }

    if (file.write(pcm16Mono) != pcm16Mono.size()) {
        return false;
    }

    return true;
}

VoiceSignature AudioUtils::signatureFromPcm16(const QByteArray &pcm16Mono) {
    VoiceSignature sig;
    if (pcm16Mono.isEmpty()) {
        return sig;
    }

    const int16_t *samples = reinterpret_cast<const int16_t *>(pcm16Mono.constData());
    const int count = pcm16Mono.size() / static_cast<int>(sizeof(int16_t));
    if (count <= 0) {
        return sig;
    }

    double sumSq = 0.0;
    int zeroCrossings = 0;

    int16_t prev = samples[0];
    for (int i = 0; i < count; ++i) {
        const int16_t s = samples[i];
        const double n = static_cast<double>(s) / 32768.0;
        sumSq += n * n;

        if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0)) {
            ++zeroCrossings;
        }
        prev = s;
    }

    sig.rms = std::sqrt(sumSq / count);
    sig.zcr = static_cast<double>(zeroCrossings) / count;
    return sig;
}
