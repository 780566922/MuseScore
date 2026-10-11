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

#include "audio/engine/internal/codecs/audiodecoder.h"
#include "audio/engine/isynthresolver.h"

#include "modularity/ioc.h"

#include "log.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;

static const std::string PLAYBACK_MODULE_NAME("playback");

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

    m_resolver->setClipData(id, std::make_shared<codec::AudioData>(std::move(decoded)), params);
}

void AudioClipResolver::clearClipData(const AudioResourceId& id)
{
    m_resolver->clearClipData(id);
}

bool AudioClipResolver::isDecodable(const ByteArray& data) const
{
    codec::AudioData decoded;
    return codec::AudioDecoder::decodeMemory(data, decoded);
}
