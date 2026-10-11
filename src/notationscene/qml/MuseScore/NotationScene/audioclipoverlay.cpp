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
static constexpr size_t CLIP_WAVE_BUCKETS = 1500;
static constexpr double LANE_HEIGHT_SPATIUMS = 3.0;

//! NOTE Chosen to read on both the light and the dark score.
static const QColor CLIP_COLOR = QColor(70, 150, 240);

AudioClipOverlay::AudioClipOverlay(const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_probe(iocCtx)
{
    if (waveProvider()) {
        waveProvider()->clipWaveChanged().onNotify(this, [this]() {
            update();
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

    if (!m_notation || !m_wave.isValid()) {
        return;
    }

    const Score* score = m_notation->elements()->msScore();
    const Measure* lastMeasure = score ? score->lastMeasureMM() : nullptr;
    if (!lastMeasure) {
        return;
    }

    //! NOTE Only what falls inside the score can be seen or heard, so it is
    //! that part of the clip that is spread over the staves.
    const int scoreEndTick = lastMeasure->endTick().ticks();
    const int startTick = m_wave.startTick + m_dragDeltaTicks;
    const int endTick = m_wave.endTick + m_dragDeltaTicks;

    if (endTick <= startTick) {
        return;
    }

    const int drawnStart = std::max(startTick, 0);
    const int drawnEnd = std::min(endTick, scoreEndTick);

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

void AudioClipOverlay::paint(muse::draw::Painter* painter)
{
    if (m_spans.empty()) {
        return;
    }

    QColor fill = CLIP_COLOR;
    fill.setAlpha(m_dragging ? 120 : 70);

    QColor line = CLIP_COLOR;
    line.setAlpha(220);

    const double edgeWidth = std::max(0.1 * m_spatium, 0.1);
    const double waveWidth = std::max(0.06 * m_spatium, 0.06);

    std::vector<LineF> waves;

    for (const Span& span : m_spans) {
        const RectF& lane = span.lane;

        painter->setPen(Pen(line, edgeWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->setBrush(fill);
        painter->drawRoundedRect(lane, 0.5 * m_spatium, 0.5 * m_spatium);

        const size_t pointCount = size_t(std::clamp(lane.width() / (m_spatium * 0.25), 2.0, 1500.0));
        const double centerY = lane.center().y();
        const double maxAmplitude = lane.height() * 0.42;

        waves.clear();
        waves.reserve(pointCount);

        for (size_t point = 0; point < pointCount; ++point) {
            const double fraction = double(point) / double(pointCount - 1);
            const double amplitude = std::max(peakAt(span, fraction) * maxAmplitude, 0.05 * m_spatium);
            const double x = lane.left() + fraction * lane.width();

            waves.emplace_back(PointF(x, centerY - amplitude), PointF(x, centerY + amplitude));
        }

        painter->setPen(Pen(line, waveWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin));
        painter->drawLines(waves);
    }
}

bool AudioClipOverlay::hitTest(const PointF& logicalPos) const
{
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

    //! NOTE The clip lines up with the grid, so the drag is taken in beats.
    const double ticks = (logicalPos.x() - m_dragStartPos.x()) * m_ticksPerUnit;
    const int delta = int(std::round(ticks / Constants::DIVISION) * Constants::DIVISION);

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
