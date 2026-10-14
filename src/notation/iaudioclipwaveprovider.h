/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

namespace mu::notation {
//! The audio clip of the score, in the shape it is drawn in.
struct AudioClipWave {
    int startTick = 0;          //! where on the score the clip starts
    int endTick = 0;            //! where on the score the part that is heard ends
    std::vector<float> peaks;   //! furthest the wave swings up, in [0, 1]
    std::vector<float> troughs; //! furthest it swings down, in [-1, 0]
    std::vector<float> rmses;   //! loudness on the average, in [0, 1]

    bool isValid() const { return !peaks.empty() && endTick > startTick; }
};

//! NOTE The clip belongs to the project, but it is drawn over the score, which
//! knows nothing of either the project or playback. This is what it is given.
class IAudioClipWaveProvider : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IAudioClipWaveProvider)

public:
    virtual ~IAudioClipWaveProvider() = default;

    //! The envelope is spread over `bucketCount` values however long the clip is.
    virtual AudioClipWave clipWave(size_t bucketCount) const = 0;
    virtual muse::async::Notification clipWaveChanged() const = 0;
};
}
