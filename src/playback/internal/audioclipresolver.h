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

#pragma once

#include <memory>

#include "audio/engine/internal/synthesizers/audiofile/audiofileresolver.h"

#include "playback/iaudioclipresolver.h"

namespace mu::playback {
//! Decodes audio clips and registers them with the audio engine.
//!
//! This is the only place that knows about the internals of the audio engine;
//! the rest of the playback module deals in raw bytes.
class AudioClipResolver : public IAudioClipResolver
{
public:
    AudioClipResolver();

    void setClipData(const muse::audio::AudioResourceId& id, const muse::ByteArray& data,
                     muse::secs_t startSec, muse::secs_t inSec, muse::secs_t outSec) override;
    void clearClipData(const muse::audio::AudioResourceId& id) override;
    bool isDecodable(const muse::ByteArray& data) const override;

private:
    std::shared_ptr<muse::audio::synth::AudioFileResolver> m_resolver;
};
}
