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

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "draw/types/geometry.h"

#include "notationscene/inotationsceneconfiguration.h"

#include "notation/iaudioclipwaveprovider.h"
#include "notation/inotation_fwd.h"

#include "playbackcursor.h"

namespace mu::engraving {
class Measure;
}

namespace mu::notation {
//! The audio clip attached to the score, drawn as a strip above the staves.
//!
//! Holding a modifier key and dragging it slides the clip along the score. The
//! clip is only ever moved, never shortened: the part of it that ends up before
//! the beginning of the score, or past its end, is simply not reached when the
//! score is played back or exported, and comes back when the clip is moved back
//! over the score. A marker on the strip points out when an end of the clip has
//! been pushed out of the score in this way.
//!
//! The strip is only drawn while it is asked for: it can be turned off so that
//! nothing lies over the score. The clip is heard either way, and a strip that
//! is not drawn cannot be dragged.
class AudioClipOverlay : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IAudioClipWaveProvider> waveProvider = { this };
    muse::GlobalInject<INotationSceneConfiguration> configuration;

public:
    AudioClipOverlay(const muse::modularity::ContextPtr& iocCtx);

    void setNotation(INotationPtr notation);

    //! Re-reads the clip and spreads it over the score.
    void update();

    void paint(muse::draw::Painter* painter);

    //! Whether the strip is asked for, and so is drawn and can be grabbed.
    bool isVisible() const;

    bool isValid() const;
    bool isDragging() const;

    //! Whether `logicalPos` falls on the clip, and so can be grabbed.
    bool hitTest(const muse::PointF& logicalPos) const;

    void beginDrag(const muse::PointF& logicalPos);
    void dragTo(const muse::PointF& logicalPos);
    //! Where the drag would leave the clip, in score ticks; may be negative.
    int draggedTick() const;
    void finishDrag();

    //! Sent whenever the clip changes, so that the view can be redrawn.
    muse::async::Notification clipChanged() const { return m_clipChanged; }

private:
    struct Span {
        muse::RectF lane;        //! where this piece of the clip is drawn, in logical coordinates
        double peakFrom = 0.0;   //! which part of the envelope it covers, in [0, 1]
        double peakTo = 1.0;
    };

    void rebuildSpans();

    double xForTick(int tick) const;
    muse::RectF bandForTick(int tick) const;
    double measureRightX(const engraving::Measure* measure) const;
    double ticksPerUnitNear(int tick) const;
    double peakAt(const Span& span, double fraction) const;

    INotationPtr m_notation;
    //! NOTE Borrowed to turn a tick into a place on the score; it is only ever
    //! asked where a tick would be, and never drawn.
    mutable PlaybackCursor m_probe;

    AudioClipWave m_wave;
    std::vector<Span> m_spans;
    double m_spatium = 1.0;

    //! Whether an end of the clip has been slid out of the score, and so is
    //! neither drawn nor heard.
    bool m_offScoreStart = false;
    bool m_offScoreEnd = false;

    bool m_dragging = false;
    muse::PointF m_dragStartPos;
    int m_dragDeltaTicks = 0;
    double m_ticksPerUnit = 1.0;

    muse::async::Notification m_clipChanged;
};
}
