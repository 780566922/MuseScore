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

#include "audioclipoverlay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QColor>

#include "draw/painter.h"
#include "draw/types/brush.h"
#include "draw/types/pen.h"
#include "draw/types/transform.h"

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/types/constants.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationstyle.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

namespace mu::notation {
//! How much of the clip one drawn value of the wave stands for. The strip is drawn
//! about a point per pixel wide, so this covers a score of an hour; asking for
//! values past the pixels there are costs nothing but the memory they take.
static constexpr size_t CLIP_WAVE_BUCKETS = 200000;
static constexpr double LANE_HEIGHT_SPATIUMS = 5.0;

//! NOTE How far apart, in pixels, the points of the drawn wave are put - near
//! enough that it reads as a wave rather than as a smoothed-out ribbon - and the
//! most points one staff line's worth of it is drawn with, so that a score of any
//! length cannot slow the drawing down without bound.
static constexpr double CLIP_WAVE_STEP_PX = 0.6;
static constexpr double CLIP_WAVE_MAX_POINTS = 60000.0;

//! NOTE Chosen to read on both the light and the dark score. The wash over the
//! staves stays see-through, so that what is underneath it can still be lined
//! up against, while everything drawn on top of the wash is opaque.
static const QColor CLIP_COLOR = QColor(26, 118, 214);

//! NOTE Drawn on top of the wash: the wave, the edges, the measures and the
//! beats, which are what has to be read at a glance, so they are darker than the
//! wash rather than the same colour as it.
static const QColor CLIP_INK = QColor(10, 62, 132);

AudioClipOverlay::AudioClipOverlay(const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_probe(iocCtx)
{
    if (waveProvider()) {
        waveProvider()->clipWaveChanged().onNotify(this, [this]() {
            update();
            m_clipChanged.notify();
        });
    }

    if (configuration()) {
        configuration()->audioClipVisibleChanged().onNotify(this, [this]() {
            m_clipChanged.notify();
        });
    }
}

void AudioClipOverlay::setNotation(INotationPtr notation)
{
    m_notation = notation;
    m_probe.setNotation(notation);

    m_spatium = m_notation ? m_notation->style()->styleValue(StyleId::spatium).toDouble() : 1.0;
    if (m_spatium <= 0.0) {
        m_spatium = 1.0;
    }

    update();
}

void AudioClipOverlay::update()
{
    m_wave = AudioClipWave();

    if (m_notation && waveProvider()) {
        m_wave = waveProvider()->clipWave(CLIP_WAVE_BUCKETS);
    }

    rebuildStrips();
}

bool AudioClipOverlay::isVisible() const
{
    return configuration() && configuration()->isAudioClipVisible();
}

bool AudioClipOverlay::isValid() const
{
    return !m_strips.empty();
}

bool AudioClipOverlay::isDragging() const
{
    return m_dragging;
}

void AudioClipOverlay::rebuildStrips()
{
    m_strips.clear();
    m_offScoreStart = false;
    m_offScoreEnd = false;

    if (!m_notation || !m_wave.isValid()) {
        return;
    }

    const Score* score = m_notation->elements()->msScore();
    const Measure* lastMeasure = score ? score->lastMeasureMM() : nullptr;
    if (!lastMeasure) {
        return;
    }

    //! NOTE Only what falls inside the score can be seen or heard, so it is that
    //! part of the clip that is spread over the staves. The clip itself keeps its
    //! whole length; see the note in moveAudioClip.
    const int scoreEndTick = lastMeasure->endTick().ticks();
    const int startTick = m_wave.startTick + m_dragDeltaTicks;
    const int endTick = m_wave.endTick + m_dragDeltaTicks;

    if (endTick <= startTick) {
        return;
    }

    const int drawnStart = std::max(startTick, 0);
    const int drawnEnd = std::min(endTick, scoreEndTick);

    m_offScoreStart = drawnStart > startTick;
    m_offScoreEnd = drawnEnd < endTick;

    if (drawnEnd <= drawnStart) {
        return;
    }

    //! NOTE The clip is drawn in one piece per staff line rather than in as many
    //! pieces as there are measures, because it is one sound and it reads as one.
    //! Every mark on it is read off the one map of ticks to places that the score
    //! lays its own notes out with, so a peak of the wave, the measure it is meant
    //! to fall on, and the beat it is meant to fall on cannot drift apart.
    const Measure* measure = score->tick2measureMM(Fraction::fromTicks(drawnStart));

    while (measure) {
        if (measure->tick().ticks() >= drawnEnd) {
            break;
        }

        const System* system = measure->system();
        if (!system || !system->page()) {
            measure = measure->nextMeasureMM();
            continue;
        }

        Strip strip;
        std::vector<const Measure*> measures;

        int systemStart = measure->tick().ticks();
        int systemEnd = systemStart;

        for (; measure && measure->system() == system; measure = measure->nextMeasureMM()) {
            appendAnchors(strip.anchors, measure);
            measures.push_back(measure);
            systemEnd = measure->endTick().ticks();
        }

        strip.fromTick = std::max(drawnStart, systemStart);
        strip.toTick = std::min(drawnEnd, systemEnd);

        if (strip.toTick > strip.fromTick && strip.anchors.size() >= 2) {
            strip.fromX = xForTick(strip, strip.fromTick);
            strip.toX = xForTick(strip, strip.toTick);

            //! NOTE Where the staves of this staff line sit is the one thing the
            //! score is asked rather than worked out, there being no telling it
            //! from the ticks alone.
            m_probe.move(strip.fromTick);
            const RectF probeRect = m_probe.rect();

            if (probeRect.isValid() && strip.toX > strip.fromX) {
                strip.bandTop = probeRect.top();
                appendMarks(strip, measures);
                m_strips.push_back(std::move(strip));
            }
        }
    }
}

//! NOTE A measure ends at its closing bar line, and the notes in it sit where the
//! score has placed them. Between one of those places and the next the score
//! spreads the time in a straight line, so that is what is written down here, and
//! what every tick is read back through.
//!
//! The room a staff line keeps clear before its first note - for the clef, or for
//! the time signature - is left out of this on purpose: it is not time, and
//! stretching the first beat of a measure across it would set that beat down in the
//! wrong place. A place already written down for an earlier tick is kept as it is,
//! so that a bar line is read as the bar line rather than as the note that follows.
void AudioClipOverlay::appendAnchors(std::vector<Anchor>& anchors, const Measure* measure) const
{
    auto append = [&anchors](int tick, double x) {
        if (anchors.empty() || anchors.back().tick < tick) {
            anchors.push_back(Anchor { tick, x });
        }
    };

    for (const Segment* segment = measure->first(SegmentType::ChordRest); segment;
         segment = segment->next(SegmentType::ChordRest)) {
        if (segment->visible()) {
            append(segment->tick().ticks(), segment->canvasPos().x());
        }
    }

    append(measure->endTick().ticks(), measureRightX(measure));
}

//! NOTE The right edge of a measure that ends a staff line is the margin, which no
//! tick resolves to, so it is taken from the measure itself.
double AudioClipOverlay::measureRightX(const Measure* measure) const
{
    const Segment* endBarLine = measure->findSegment(SegmentType::EndBarLine, measure->endTick());
    if (endBarLine) {
        return endBarLine->canvasPos().x();
    }

    return measure->canvasPos().x() + measure->width();
}

void AudioClipOverlay::appendMarks(Strip& strip, const std::vector<const Measure*>& measures) const
{
    for (const Measure* measure : measures) {
        const int measureStart = measure->tick().ticks();
        const int measureEnd = measure->endTick().ticks();

        if (measureStart > strip.fromTick && measureStart < strip.toTick) {
            strip.barXs.push_back(xForTick(strip, measureStart));
        }

        for (int tick = measureStart + Constants::DIVISION; tick < measureEnd; tick += Constants::DIVISION) {
            if (tick > strip.fromTick && tick < strip.toTick) {
                strip.beatXs.push_back(xForTick(strip, tick));
            }
        }
    }
}

RectF AudioClipOverlay::laneOf(const Strip& strip) const
{
    return RectF(strip.fromX, strip.bandTop - LANE_HEIGHT_SPATIUMS * m_spatium * 0.5,
                 strip.toX - strip.fromX, LANE_HEIGHT_SPATIUMS * m_spatium);
}

double AudioClipOverlay::xForTick(const Strip& strip, double tick) const
{
    const std::vector<Anchor>& anchors = strip.anchors;

    if (anchors.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (anchors.size() == 1 || tick <= double(anchors.front().tick)) {
        return anchors.front().x;
    }

    if (tick >= double(anchors.back().tick)) {
        return anchors.back().x;
    }

    size_t low = 0;
    size_t high = anchors.size() - 1;

    while (high - low > 1) {
        const size_t middle = (low + high) / 2;
        if (double(anchors[middle].tick) <= tick) {
            low = middle;
        } else {
            high = middle;
        }
    }

    const double span = double(anchors[high].tick - anchors[low].tick);
    const double fraction = span > 0.0 ? (tick - double(anchors[low].tick)) / span : 0.0;

    return anchors[low].x + fraction * (anchors[high].x - anchors[low].x);
}

double AudioClipOverlay::tickForX(const std::vector<Anchor>& anchors, double x) const
{
    if (anchors.empty()) {
        return 0.0;
    }

    if (anchors.size() == 1) {
        return double(anchors.front().tick);
    }

    auto slopeAt = [&anchors](size_t index) {
        const double run = anchors[index + 1].x - anchors[index].x;
        return run > 0.0 ? double(anchors[index + 1].tick - anchors[index].tick) / run : 0.0;
    };

    if (x <= anchors.front().x) {
        return double(anchors.front().tick) + (x - anchors.front().x) * slopeAt(0);
    }

    if (x >= anchors.back().x) {
        return double(anchors.back().tick) + (x - anchors.back().x) * slopeAt(anchors.size() - 2);
    }

    size_t low = 0;
    size_t high = anchors.size() - 1;

    while (high - low > 1) {
        const size_t middle = (low + high) / 2;
        if (anchors[middle].x <= x) {
            low = middle;
        } else {
            high = middle;
        }
    }

    const double run = anchors[high].x - anchors[low].x;
    const double fraction = run > 0.0 ? (x - anchors[low].x) / run : 0.0;

    return double(anchors[low].tick) + fraction * double(anchors[high].tick - anchors[low].tick);
}

//! NOTE The loudest the clip gets across a stretch of it, rather than its value at
//! one point of it: a drum hit lasts less than a point of the drawn wave, and
//! taking the loudest is what keeps such a hit standing out once the whole clip
//! has been squeezed onto the score.
float AudioClipOverlay::peakBetween(int firstTick, int lastTick) const
{
    const size_t count = m_wave.peaks.size();
    const int span = m_wave.endTick - m_wave.startTick;

    if (count == 0 || span <= 0) {
        return 0.f;
    }

    double from = double(firstTick - m_wave.startTick) / double(span);
    double to = double(lastTick - m_wave.startTick) / double(span);

    if (to < from) {
        std::swap(from, to);
    }

    const double lastIndex = double(count - 1);
    const size_t first = size_t(std::llround(std::clamp(from, 0.0, 1.0) * lastIndex));
    const size_t last = size_t(std::llround(std::clamp(to, 0.0, 1.0) * lastIndex));

    float peak = 0.f;
    for (size_t index = first; index <= last; ++index) {
        peak = std::max(peak, m_wave.peaks[index]);
    }

    return peak;
}

void AudioClipOverlay::paintMarks(Painter* painter, const Strip& strip, const std::vector<double>& xs,
                                 double inset, const QColor& color, double width) const
{
    if (xs.empty()) {
        return;
    }

    const RectF lane = laneOf(strip);

    std::vector<LineF> marks;
    marks.reserve(xs.size());

    for (const double x : xs) {
        marks.emplace_back(PointF(x, lane.top() + inset), PointF(x, lane.bottom() - inset));
    }

    painter->setPen(Pen(color, width, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
    painter->drawLines(marks);
}

//! NOTE The wave is walked along the score rather than along the clip: every point
//! of it is put where the score puts the tick it stands for, so a peak sits over
//! the beat it is meant to fall on however unevenly the score has spread its notes
//! out. The stretch of the clip each point stands for decides how loud that point
//! is drawn, and the points are put as near together as the screen can show.
void AudioClipOverlay::paintWave(Painter* painter, const Strip& strip, double pixelsPerUnit,
                                 const QColor& color, double width) const
{
    const RectF lane = laneOf(strip);
    const double length = lane.width();

    if (length <= 0.0 || strip.anchors.size() < 2) {
        return;
    }

    const double step = std::max(CLIP_WAVE_STEP_PX / std::max(pixelsPerUnit, 0.01), length / CLIP_WAVE_MAX_POINTS);

    const double centerY = lane.center().y();
    //! NOTE A little of the strip is left clear above and below the wave, so that
    //! the measures drawn over it can still be made out where the clip is loudest.
    const double maxAmplitude = lane.height() * 0.42;
    const double minAmplitude = 0.02 * m_spatium;

    std::vector<LineF> waves;
    waves.reserve(size_t(length / step) + 2);

    int tickBefore = strip.fromTick;

    for (size_t index = 0; index + 1 < strip.anchors.size(); ++index) {
        const int tickFrom = std::max(strip.anchors[index].tick, strip.fromTick);
        const int tickTo = std::min(strip.anchors[index + 1].tick, strip.toTick);

        if (tickTo <= tickFrom) {
            continue;
        }

        const double xFrom = xForTick(strip, tickFrom);
        const double xTo = xForTick(strip, tickTo);
        const int steps = std::max(1, int(std::ceil((xTo - xFrom) / step)));

        for (int taken = 1; taken <= steps; ++taken) {
            const double fraction = double(taken) / double(steps);
            const double x = xFrom + (xTo - xFrom) * fraction;
            const int tick = int(std::lround(tickFrom + (tickTo - tickFrom) * fraction));

            const float peak = peakBetween(tickBefore - m_dragDeltaTicks,
                                           std::max(tick, tickBefore + 1) - m_dragDeltaTicks);
            const double amplitude = std::clamp(double(peak) * maxAmplitude, minAmplitude, maxAmplitude);

            waves.emplace_back(PointF(x, centerY - amplitude), PointF(x, centerY + amplitude));

            tickBefore = tick;
        }
    }

    painter->setPen(Pen(color, width, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
    painter->drawLines(waves);
}

void AudioClipOverlay::paint(Painter* painter)
{
    if (m_strips.empty() || !isVisible()) {
        return;
    }

    QColor fill = CLIP_COLOR;
    fill.setAlpha(m_dragging ? 150 : 95);

    QColor ink = CLIP_INK;
    ink.setAlpha(230);

    QColor bar = CLIP_INK;
    bar.setAlpha(255);

    QColor beat = CLIP_INK;
    beat.setAlpha(105);

    const double edgeWidth = std::max(0.14 * m_spatium, 0.14);
    const double waveWidth = std::max(0.13 * m_spatium, 0.13);
    const double barWidth = std::max(0.16 * m_spatium, 0.16);
    const double beatWidth = std::max(0.07 * m_spatium, 0.07);

    const double corner = 0.5 * m_spatium;
    const double pixelsPerUnit = std::abs(painter->worldTransform().m11());

    for (const Strip& strip : m_strips) {
        painter->setPen(Pen(CLIP_INK, edgeWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->setBrush(fill);
        painter->drawRoundedRect(laneOf(strip), corner, corner);

        //! NOTE The wave is drawn first and the measures and beats over it, so
        //! that where a measure falls can still be made out over the loudest part
        //! of the clip - which is the one thing that has to be found at a glance
        //! when the clip is lined up.
        paintWave(painter, strip, pixelsPerUnit, ink, waveWidth);
        paintMarks(painter, strip, strip.beatXs, 0.0, beat, beatWidth);
        paintMarks(painter, strip, strip.barXs, 0.0, bar, barWidth);
    }

    if (!m_offScoreStart && !m_offScoreEnd) {
        return;
    }

    //! NOTE The part of the clip that has been slid out of the score is drawn
    //! nowhere, there being no room for it, so without a marker the edge it went
    //! out through would look like the clip simply stopping there.
    const double markerSize = 0.9 * m_spatium;

    auto paintMarker = [&](const RectF& lane, bool atStart) {
        const double x = atStart ? lane.left() : lane.right();
        const double y = lane.center().y();
        const double tip = atStart ? x - markerSize : x + markerSize;

        const PointF triangle[] = {
            PointF(x, y - markerSize),
            PointF(tip, y),
            PointF(x, y + markerSize),
        };

        painter->setPen(Pen(ink, edgeWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->setBrush(Brush(ink));
        painter->drawPolygon(triangle, 3);
    };

    if (m_offScoreStart) {
        paintMarker(laneOf(m_strips.front()), true);
    }

    if (m_offScoreEnd) {
        paintMarker(laneOf(m_strips.back()), false);
    }
}

bool AudioClipOverlay::hitTest(const PointF& logicalPos) const
{
    if (!isVisible()) {
        return false;
    }

    for (const Strip& strip : m_strips) {
        //! NOTE The strip is thin, so a little slack makes it easier to grab.
        if (laneOf(strip).adjusted(0.0, -m_spatium, 0.0, m_spatium).contains(logicalPos)) {
            return true;
        }
    }

    return false;
}

void AudioClipOverlay::beginDrag(const PointF& logicalPos)
{
    for (const Strip& strip : m_strips) {
        if (!laneOf(strip).adjusted(0.0, -m_spatium, 0.0, m_spatium).contains(logicalPos)) {
            continue;
        }

        //! NOTE What the pointer is over is what is followed, so the tick it is
        //! over is taken from the staff line it was grabbed on, and kept for the
        //! rest of the drag.
        m_dragAnchors = strip.anchors;
        m_dragAnchorTick = int(std::lround(tickForX(m_dragAnchors, logicalPos.x())));
        m_dragging = true;
        m_dragDeltaTicks = 0;
        return;
    }
}

void AudioClipOverlay::dragTo(const PointF& logicalPos)
{
    if (!m_dragging) {
        return;
    }

    //! NOTE The clip is lined up by ear rather than by the grid, so the drag is
    //! followed as closely as the score can express it - the tick that was under
    //! the pointer stays under the pointer, what the score has drawn between two
    //! ticks being the only limit - and nothing is snapped.
    const int delta = int(std::llround(tickForX(m_dragAnchors, logicalPos.x()))) - m_dragAnchorTick;

    if (delta == m_dragDeltaTicks) {
        return;
    }

    m_dragDeltaTicks = delta;
    rebuildStrips();
}

int AudioClipOverlay::draggedTick() const
{
    return m_wave.startTick + m_dragDeltaTicks;
}

void AudioClipOverlay::finishDrag()
{
    m_dragging = false;
    m_dragAnchors.clear();
    m_dragDeltaTicks = 0;
    rebuildStrips();
}
}
