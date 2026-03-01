#pragma once

#include "MemoryStore.h"

#include <QByteArray>
#include <QString>

namespace AudioUtils {

bool writePcm16Wav(const QString &path, const QByteArray &pcm16Mono, int sampleRate);
VoiceSignature signatureFromPcm16(const QByteArray &pcm16Mono);

}
