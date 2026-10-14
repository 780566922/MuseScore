/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "audioclipresolver.h"

#include <algorithm>
#include <cmath>

#include "audio/engine/internal/codecs/audiodecoder.h"
#include "audio/engine/isynthresolver.h"

#include "modularity/ioc.h"

#include "log.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;

static const std::string PLAYBACK_MODULE_NAME("playback");

//! The most values an envelope is allowed to hold; a clip of any length then
//! costs about the same to draw. The values are what the wave is drawn from, so
//! a clip short enough to be held one value per sample is: a wave drawn from
//! anything coarser comes out in steps rather than as a wave.
static constexpr size_t MAX_ENVELOPE_BUCKETS = 200000;

static AudioClipResolver::Envelope makeEnvelope(const codec::AudioData& data)
{
    AudioClipResolver::Envelope envelope;

    if (!data.isValid()) {
        return envelope;
    }

    const size_t framesCount = data.framesCount();
    const double totalSec = double(framesCount) / data.sampleRate;

    envelope.totalSec = totalSec;
    envelope.bucketSecs = std::max(1.0 / data.sampleRate, totalSec / double(MAX_ENVELOPE_BUCKETS));

    const size_t framesPerBucket = std::max<size_t>(1, size_t(std::llround(data.sampleRate * envelope.bucketSecs)));
    const size_t bucketCount = (framesCount + framesPerBucket - 1) / framesPerBucket;
    envelope.peaks.resize(bucketCount, 0.f);
    envelope.troughs.resize(bucketCount, 0.f);
    envelope.rmses.resize(bucketCount, 0.f);

    for (size_t bucket = 0; bucket < bucketCount; ++bucket) {
        const size_t firstFrame = bucket * framesPerBucket;
        const size_t lastFrame = std::min(firstFrame + framesPerBucket, framesCount);

        float peak = 0.f;
        float trough = 0.f;
        double sumOfSquares = 0.0;
        size_t values = 0;

        for (size_t frame = firstFrame; frame < lastFrame; ++frame) {
            for (size_t channel = 0; channel < data.channels; ++channel) {
                const float value = data.samples[frame * data.channels + channel];

                peak = std::max(peak, value);
                trough = std::min(trough, value);

                sumOfSquares += double(value) * double(value);
                ++values;
            }
        }

        envelope.peaks[bucket] = peak;
        envelope.troughs[bucket] = trough;
        envelope.rmses[bucket] = values > 0 ? float(std::sqrt(sumOfSquares / double(values))) : 0.f;
    }

    return envelope;
}

AudioClipResolver::AudioClipResolver()
{
    m_resolver = std::make_shared<synth::AudioFileResolver>();

    // The engine only knows how to build a source for a type it has a resolver
    // for, so this has to happen before any score is played.
    auto synthResolver = modularity::globalIoc()->resolve<synth::ISynthResolver>(PLAYBACK_MODULE_NAME);
    if (synthResolver) {
        synthResolver->registerResolver(AudioSourceType::AudioFile, m_resolver);
    } else {
        LOGE() << "unable to register the audio file resolver";
    }
}

void AudioClipResolver::setClipData(const AudioResourceId& id, const ByteArray& data,
                                    secs_t startSec, secs_t inSec, secs_t outSec)
{
    codec::AudioData decoded;

    if (!codec::AudioDecoder::decodeMemory(data, decoded)) {
        LOGE() << "unable to decode the audio clip: " << id;
        return;
    }

    LOGI() << "decoded audio clip '" << id << "': " << decoded.sampleRate << " Hz, "
           << decoded.channels << " channel(s), "
           << static_cast<double>(decoded.framesCount()) / decoded.sampleRate << " s, "
           << "starts at " << startSec << " s, trim " << inSec << ".." << outSec;

    synth::AudioClipParams params;
    params.startSec = startSec;
    params.inSec = inSec;
    params.outSec = outSec;

    m_envelopes[id] = makeEnvelope(decoded);
    m_resolver->setClipData(id, std::make_shared<codec::AudioData>(std::move(decoded)), params);
}

void AudioClipResolver::clearClipData(const AudioResourceId& id)
{
    m_envelopes.erase(id);
    m_resolver->clearClipData(id);
}

bool AudioClipResolver::clipEnvelope(const AudioResourceId& id, secs_t inSec, secs_t outSec,
                                     size_t bucketCount, AudioClipEnvelope& result) const
{
    result = AudioClipEnvelope();

    auto search = m_envelopes.find(id);
    if (search == m_envelopes.end() || bucketCount == 0) {
        return false;
    }

    const Envelope& envelope = search->second;
    if (!envelope.isValid()) {
        return false;
    }

    //! NOTE Only the part of the clip that is heard is drawn, so it is this
    //! range that is spread over the buckets the caller asked for.
    const double fromSec = std::clamp(double(inSec), 0.0, envelope.totalSec);
    const double toSec = double(outSec) < 0.0
                         ? envelope.totalSec
                         : std::clamp(double(outSec), 0.0, envelope.totalSec);
    if (toSec <= fromSec) {
        return false;
    }

    result.fromSec = fromSec;
    result.toSec = toSec;
    result.totalSec = envelope.totalSec;

    //! NOTE Asking for more values than the envelope holds would only repeat the
    //! ones it has, so the ask is kept down to what there is to tell.
    const size_t buckets = std::min(bucketCount, envelope.peaks.size());
    result.peaks.resize(buckets, 0.f);
    result.troughs.resize(buckets, 0.f);
    result.rmses.resize(buckets, 0.f);

    const double bucketSecs = (toSec - fromSec) / buckets;

    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        const size_t firstIndex = std::min(size_t((fromSec + bucket * bucketSecs) / envelope.bucketSecs),
                                           envelope.peaks.size() - 1);
        const size_t lastIndex = std::clamp(size_t(std::ceil((fromSec + (bucket + 1) * bucketSecs) / envelope.bucketSecs)),
                                            firstIndex + 1, envelope.peaks.size());

        //! NOTE A stretch of the clip that is drawn as one value keeps the
        //! furthest it swings either way, so that a spike is not smoothed away
        //! however far the track has been zoomed out.
        float peak = 0.f;
        float trough = 0.f;
        double sumOfSquares = 0.0;
        size_t values = 0;

        for (size_t index = firstIndex; index < lastIndex; ++index) {
            peak = std::max(peak, envelope.peaks[index]);

            if (index < envelope.troughs.size()) {
                trough = std::min(trough, envelope.troughs[index]);
            }

            //! NOTE Loudness is not a greatest-of, the way a swing is: several
            //! stretches together are as loud as the square mean of them all,
            //! which is what keeps the body of the wave from filling up to its
            //! outline as the track is zoomed out.
            if (index < envelope.rmses.size()) {
                sumOfSquares += double(envelope.rmses[index]) * double(envelope.rmses[index]);
                ++values;
            }
        }

        result.peaks[bucket] = peak;
        result.troughs[bucket] = trough;
        result.rmses[bucket] = values > 0 ? float(std::sqrt(sumOfSquares / double(values))) : 0.f;
    }

    return true;
}

bool AudioClipResolver::isDecodable(const ByteArray& data) const
{
    codec::AudioData decoded;
    return codec::AudioDecoder::decodeMemory(data, decoded);
}
