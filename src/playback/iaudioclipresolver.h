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

#include <vector>

#include "modularity/imoduleinterface.h"

#include "audio/common/audiotypes.h"
#include "global/types/bytearray.h"

namespace mu::playback {
//! The part of an audio clip that is heard, in the shape it is drawn in.
//!
//! A wave drawn from loudness alone comes out as a band of even height, which
//! says nothing about the shape of the sound; so each stretch is told three
//! ways: how far it swings up, how far it swings down, and how loud it is on
//! the average. The first two are what give the wave its outline, and the last
//! is what gives it a body, the way a wave is drawn in an editor.
struct AudioClipEnvelope {
    double fromSec = 0.0;            //! where the part that is heard begins inside the clip
    double toSec = 0.0;              //! where it ends
    double totalSec = 0.0;           //! how long the whole clip is
    std::vector<float> peaks;        //! furthest the wave swings up, in [0, 1]
    std::vector<float> troughs;      //! furthest it swings down, in [-1, 0]
    std::vector<float> rmses;        //! loudness on the average, in [0, 1]

    bool isValid() const { return toSec > fromSec && !peaks.empty(); }
};

//! Hands audio clips attached to a score over to the audio engine.
//!
//! Unlike a soundfont or a plugin, an audio clip is not a globally available
//! resource: it belongs to one particular score. The decoded data therefore has
//! to be handed over before the score starts playing, which is what this
//! interface is for.
class IAudioClipResolver : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IAudioClipResolver)

public:
    virtual ~IAudioClipResolver() = default;

    //! `data` holds the raw bytes of the audio file, in whatever format the
    //! decoder understands. `startSec` is where on the playback timeline the
    //! clip starts, `inSec` and `outSec` trim the clip itself; a negative
    //! `outSec` means "play on until the end of the clip".
    //!
    //! This must be called before the track is added to the engine.
    virtual void setClipData(const muse::audio::AudioResourceId& id, const muse::ByteArray& data,
                             muse::secs_t startSec, muse::secs_t inSec, muse::secs_t outSec) = 0;

    virtual void clearClipData(const muse::audio::AudioResourceId& id) = 0;

    //! The envelope of a clip handed over earlier, for drawing it on the score.
    //! `envelope.peaks`, `envelope.troughs` and `envelope.rmses` each receive
    //! `bucketCount` values spread over the part of the clip between `inSec` and
    //! `outSec`; a negative `outSec` means "to the end of the clip". Returns
    //! false if there is no such clip.
    virtual bool clipEnvelope(const muse::audio::AudioResourceId& id, muse::secs_t inSec, muse::secs_t outSec,
                              size_t bucketCount, AudioClipEnvelope& envelope) const = 0;

    //! Whether `data` is something the decoder can make sense of, so that a
    //! file the user picked can be turned down before it is stored.
    virtual bool isDecodable(const muse::ByteArray& data) const = 0;
};
}
