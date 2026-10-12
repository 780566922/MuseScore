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

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/types/constants.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationstyle.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;

namespace mu::notation {
//! How many values the envelope is asked for, and how tall the strip is; both
//! chosen so that the clip reads the same at any zoom level.
static constexpr size_t CLIP_WAVE_BUCKETS = 8000;
static constexpr double LANE_HEIGHT_SPATIUMS = 5.0;

//! NOTE Chosen to read on both the light and the dark score. The wash over the
//! staves stays see-through, so that what is underneath it can still be lined
//! up against, while everything drawn on top of the wash is opaque.
static const QColor CLIP_COLOR = QColor(26, 118, 214);

//! NOTE Drawn on top of the wash: the wave, the edges and the beats, which are
//! what has to be read at a glance, so they are darker than the wash rather
//! than the same colour as it.
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

    rebuildSpans();
}

bool AudioClipOverlay::isVisible() const
{
    return configuration() && configuration()->isAudioClipVisible();
}

bool AudioClipOverlay::isValid() const
{
    return !m_spans.empty();
}

bool AudioClipOverlay::isDragging() const
{
    return m_dragging;
}

void AudioClipOverlay::rebuildSpans()
{
    m_spans.clear();
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

    //! NOTE Only what falls inside the score can be seen or heard, so it is
    //! that part of the clip that is spread over the staves. The clip itself
    //! keeps its whole length; see the note in moveAudioClip.
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

    const double range = double(endTick - startTick);
    auto fractionAt = [startTick, range](int tick) {
        return double(tick - startTick) / range;
    };

    for (const Measure* measure = score->tick2measureMM(Fraction::fromTicks(drawnStart)); measure;) {
        const int measureStart = measure->tick().ticks();
        if (measureStart >= drawnEnd) {
            break;
        }

        const int measureEnd = measure->endTick().ticks();
        const int from = std::max(drawnStart, measureStart);
        const int to = std::min(drawnEnd, measureEnd);

        if (to > from) {
            const RectF band = bandForTick(from);
            const double x0 = xForTick(from);

            //! NOTE The right edge of a measure that ends a system is the
            //! margin, which no tick resolves to, so it is taken from the
            //! measure itself.
            const double x1 = to == measureEnd ? measureRightX(measure) : xForTick(to);

            if (band.isValid() && x1 > x0) {
                Span span;
                span.lane = RectF(x0, band.top() - LANE_HEIGHT_SPATIUMS * m_spatium * 0.5,
                                  x1 - x0, LANE_HEIGHT_SPATIUMS * m_spatium);
                //! NOTE Where the beats fall is found now rather than when the
                //! strip is drawn, so that a redraw costs nothing for it.
                for (int tick = measureStart + Constants::DIVISION; tick < measureEnd; tick += Constants::DIVISION) {
                    const double beatX = xForTick(tick);
                    if (std::isfinite(beatX) && beatX > x0 && beatX < x1) {
                        span.beatXs.push_back(beatX);
                    }
                }

                span.peakFrom = fractionAt(from);
                span.peakTo = fractionAt(to);
                m_spans.push_back(span);
            }
        }

        measure = measure->nextMeasureMM();
    }
}

double AudioClipOverlay::xForTick(int tick) const
{
    m_probe.move(tick);

    const RectF rect = m_probe.rect();
    if (!rect.isValid()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //! NOTE The cursor rect is drawn a little to the left of the tick; see
    //! PlaybackCursor::calculateRect.
    return rect.left() + m_spatium;
}

RectF AudioClipOverlay::bandForTick(int tick) const
{
    m_probe.move(tick);
    return m_probe.rect();
}

double AudioClipOverlay::measureRightX(const Measure* measure) const
{
    const Segment* endBarLine = measure->findSegment(SegmentType::EndBarLine, measure->endTick());
    if (endBarLine) {
        return endBarLine->canvasPos().x();
    }

    return measure->canvasPos().x() + measure->width();
}

double AudioClipOverlay::peakAt(const Span& span, double fraction) const
{
    if (m_wave.peaks.empty()) {
        return 0.0;
    }

    const double position = span.peakFrom + fraction * (span.peakTo - span.peakFrom);
    const double lastIndex = double(m_wave.peaks.size() - 1);
    const size_t index = size_t(std::clamp(position * lastIndex + 0.5, 0.0, lastIndex));

    return m_wave.peaks[index];
}

void AudioClipOverlay::paintBeatLines(muse::draw::Painter* painter, const Span& span, const QColor& color, double width) const
{
    if (span.beatXs.empty()) {
        return;
    }

    std::vector<LineF> beats;
    beats.reserve(span.beatXs.size());

    for (const double x : span.beatXs) {
        beats.emplace_back(PointF(x, span.lane.top()), PointF(x, span.lane.bottom()));
    }

    painter->setPen(Pen(color, width, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
    painter->drawLines(beats);
}

void AudioClipOverlay::paint(muse::draw::Painter* painter)
{
    if (m_spans.empty() || !isVisible()) {
        return;
    }

    QColor fill = CLIP_COLOR;
    fill.setAlpha(m_dragging ? 150 : 95);

    QColor line = CLIP_INK;
    line.setAlpha(255);

    QColor beat = CLIP_INK;
    beat.setAlpha(105);

    const double edgeWidth = std::max(0.14 * m_spatium, 0.14);
    const double waveWidth = std::max(0.13 * m_spatium, 0.13);
    const double beatWidth = std::max(0.07 * m_spatium, 0.07);

    std::vector<LineF> waves;

    for (const Span& span : m_spans) {
        const RectF& lane = span.lane;

        painter->setPen(Pen(line, edgeWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->setBrush(fill);
        painter->drawRoundedRect(lane, 0.5 * m_spatium, 0.5 * m_spatium);

        //! NOTE The beats are put where the score puts them, so that a peak in
        //! the clip can be lined up with the beat it is meant to fall on.
        paintBeatLines(painter, span, beat, beatWidth);

        const size_t pointCount = size_t(std::clamp(lane.width() / (m_spatium * 0.15), 2.0, 3000.0));
        const double centerY = lane.center().y();
        const double maxAmplitude = lane.height() * 0.48;

        waves.clear();
        waves.reserve(pointCount);

        for (size_t point = 0; point < pointCount; ++point) {
            const double fraction = double(point) / double(pointCount - 1);
            const double amplitude = std::max(peakAt(span, fraction) * maxAmplitude, 0.02 * m_spatium);
            const double x = lane.left() + fraction * lane.width();

            waves.emplace_back(PointF(x, centerY - amplitude), PointF(x, centerY + amplitude));
        }

        painter->setPen(Pen(line, waveWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->drawLines(waves);
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

        painter->setPen(Pen(line, edgeWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->setBrush(Brush(line));
        painter->drawPolygon(triangle, 3);
    };

    if (m_offScoreStart) {
        paintMarker(m_spans.front().lane, true);
    }

    if (m_offScoreEnd) {
        paintMarker(m_spans.back().lane, false);
    }
}

bool AudioClipOverlay::hitTest(const PointF& logicalPos) const
{
    if (!isVisible()) {
        return false;
    }

    for (const Span& span : m_spans) {
        //! NOTE The strip is thin, so a little slack makes it easier to grab.
        if (span.lane.adjusted(0.0, -m_spatium, 0.0, m_spatium).contains(logicalPos)) {
            return true;
        }
    }

    return false;
}

void AudioClipOverlay::beginDrag(const PointF& logicalPos)
{
    m_ticksPerUnit = ticksPerUnitNear(m_wave.startTick);
    if (m_ticksPerUnit <= 0.0) {
        return;
    }

    m_dragging = true;
    m_dragStartPos = logicalPos;
    m_dragDeltaTicks = 0;
}

//! How many ticks one unit of the canvas is worth, measured around `tick` so
//! that a tempo change elsewhere in the score does not skew the drag.
double AudioClipOverlay::ticksPerUnitNear(int tick) const
{
    const std::pair<int, int> probes[] = {
        { tick, tick + Constants::DIVISION },
        { tick - Constants::DIVISION, tick },
        { 0, Constants::DIVISION },
    };

    for (const auto& [from, to] : probes) {
        const double x0 = xForTick(from);
        const double x1 = xForTick(to);

        if (std::isfinite(x0) && std::isfinite(x1) && x1 > x0) {
            return double(Constants::DIVISION) / (x1 - x0);
        }
    }

    return 0.0;
}

void AudioClipOverlay::dragTo(const PointF& logicalPos)
{
    if (!m_dragging) {
        return;
    }

    //! NOTE The clip is lined up by ear rather than by the grid, so the drag is
    //! followed as closely as the score can express it; nothing is snapped.
    const int delta = int(std::lround((logicalPos.x() - m_dragStartPos.x()) * m_ticksPerUnit));

    if (delta == m_dragDeltaTicks) {
        return;
    }

    m_dragDeltaTicks = delta;
    rebuildSpans();
}

int AudioClipOverlay::draggedTick() const
{
    return m_wave.startTick + m_dragDeltaTicks;
}

void AudioClipOverlay::finishDrag()
{
    m_dragging = false;
    m_dragDeltaTicks = 0;
    rebuildSpans();
}
}
