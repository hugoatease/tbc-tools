/************************************************************************

    secamdecoder.h

    ld-chroma-decoder - Colourisation filter for ld-decode

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef SECAMDECODER_H
#define SECAMDECODER_H

#include <QObject>
#include <QAtomicInt>
#include <QThread>
#include <QDebug>

#include "componentframe.h"
#include "lddecodemetadata.h"
#include "sourcevideo.h"

#include "decoder.h"
#include "sourcefield.h"

class DecoderPool;

// Decoder for SECAM sources whose chroma TBC already carries demodulated
// Dr/Db (one component per line, alternating), as produced by vhs-decode's
// native SECAM chroma path (vhsdecode/secam.py). This decoder does no FM
// demodulation of its own -- it only reconstructs simultaneous Dr/Db pairs
// (one-line hold) and writes them pre-scaled into ComponentFrame's U/V so
// that OutputWriter's existing Y'UV->R'G'B' matrix produces the correct
// SECAM colour without any change to OutputWriter itself.
class SecamDecoder : public Decoder {
public:

    struct SecamConfiguration {
        LdDecodeMetaData::VideoParameters videoParameters;
    };

    SecamDecoder();
    SecamDecoder(const SecamDecoder::SecamConfiguration &config);
    bool configure(const LdDecodeMetaData::VideoParameters &videoParameters) override;
    QThread *makeThread(QAtomicInt& abort, DecoderPool& decoderPool) override;

    /// Synchronously decode SECAM chroma fields into component frames
    void decodeFrames(const QVector<SourceField>& inputFields,
                    qint32 startIndex,
                    qint32 endIndex,
                    QVector<ComponentFrame>& componentFrames);

private:
    SecamConfiguration secamConfig;
};

class SecamThread : public DecoderThread
{
    Q_OBJECT
public:
    explicit SecamThread(QAtomicInt &abort, DecoderPool &decoderPool,
                       const SecamDecoder::SecamConfiguration &secamConfig,
                       QObject *parent = nullptr);

protected:
    void decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

private:
    // Settings
    const SecamDecoder::SecamConfiguration &secamConfig;
};

#endif // SECAMDECODER_H
