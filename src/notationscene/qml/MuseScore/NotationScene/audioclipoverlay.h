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

#include <QColor>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "draw/types/geometry.h"

#include "notationscene/inotationsceneconfiguration.h"

#include "notation/iaudioclipwaveprovider.h"
#include "notation/inotation_fwd.h"

#include "playbackcursor.h"

namespace mu::engraving {
class Measure;
class System;
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
//!
//! It is drawn in one piece per staff line rather than in as many pieces as there
//! are measures, because it is one sound; and everything drawn on it is read off
//! the one map between ticks and places on the score that the score itself lays
//! its notes out with, so the wave, the measures and the beats on the strip cannot
//! drift away from one another.
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
    //! A tick, and the place on a staff line the score has put it at.
    struct Anchor {
        int tick = 0;
        double x = 0.0;
    };

    //! One staff line's worth of the clip: which stretch of the clip is drawn on
    //! it, where, the ticks to places it is drawn through, and the marks on it.
    struct Strip {
        double bandTop = 0.0;           //! where the top of the staves of this staff line sits
        int fromTick = 0;               //! the stretch of the clip drawn here, in score ticks
        int toTick = 0;
        double fromX = 0.0;             //! and where that stretch is drawn
        double toX = 0.0;
        std::vector<Anchor> anchors;    //! ticks to places, running left to right
        std::vector<double> barXs;      //! where a measure begins, in places
        std::vector<double> beatXs;     //! where a beat falls, in places
    };

    void rebuildStrips();
    void appendAnchors(std::vector<Anchor>& anchors, const engraving::Measure* measure) const;
    void appendMarks(Strip& strip, const std::vector<const engraving::Measure*>& measures) const;
    double measureRightX(const engraving::Measure* measure) const;

    muse::RectF laneOf(const Strip& strip) const;

    //! Where a tick falls between two anchors, and which tick falls at a place.
    //! Both read the same line, so they undo one another.
    double xForTick(const Strip& strip, double tick) const;
    double tickForX(const std::vector<Anchor>& anchors, double x) const;

    //! The loudest the clip gets between two of its ticks.
    float peakBetween(int firstTick, int lastTick) const;

    void paintMarks(muse::draw::Painter* painter, const Strip& strip, const std::vector<double>& xs,
                    double inset, const QColor& color, double width) const;
    void paintWave(muse::draw::Painter* painter, const Strip& strip, double pixelsPerUnit,
                   const QColor& color, double width) const;

    INotationPtr m_notation;
    //! NOTE Borrowed to find where the staves of a staff line sit; it is only ever
    //! asked where a tick would be, and never drawn.
    PlaybackCursor m_probe;

    AudioClipWave m_wave;
    std::vector<Strip> m_strips;
    double m_spatium = 1.0;

    //! Whether an end of the clip has been slid out of the score, and so is
    //! neither drawn nor heard.
    bool m_offScoreStart = false;
    bool m_offScoreEnd = false;

    bool m_dragging = false;
    //! NOTE What the pointer is over when the drag begins is what it stays over,
    //! so the drag is followed through the places of the staff line it was grabbed
    //! on, taken then and kept for the rest of the drag.
    std::vector<Anchor> m_dragAnchors;
    int m_dragAnchorTick = 0;
    int m_dragDeltaTicks = 0;

    muse::async::Notification m_clipChanged;
};
}
