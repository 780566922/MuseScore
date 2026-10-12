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
 * it under the terms of the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "audiocliptrackview.h"

#include <algorithm>
#include <cmath>

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/types/constants.h"
#include "engraving/types/fraction.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep

using namespace mu;
using namespace mu::notation;
using namespace mu::engraving;

//! How much of the clip one drawn value of the wave stands for. Far more are
//! asked for than there are pixels to draw them on, so that a peak is never
//! smoothed away; the envelope hands back fewer when the clip holds fewer.
static constexpr size_t CLIP_WAVE_BUCKETS = 200000;

//! How many ticks a whole note is worth, and the shortest beat that is drawn.
static constexpr int TICKS_PER_WHOLE_NOTE = Constants::DIVISION * 4;
static constexpr int MIN_TICKS_PER_BEAT = Constants::DIVISION / 4;

AudioClipTrackView::AudioClipTrackView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFillColor(Qt::transparent);
}

void AudioClipTrackView::componentComplete()
{
    QQuickPaintedItem::componentComplete();

    if (globalContext()) {
        globalContext()->currentNotationChanged().onNotify(this, [this]() {
            rebuild();
        });
    }

    if (waveProvider()) {
        waveProvider()->clipWaveChanged().onNotify(this, [this]() {
            rebuild();
        });
    }

    rebuild();
}

void AudioClipTrackView::rebuild()
{
    m_wave = AudioClipWave();
    m_barTicks.clear();
    m_beatTicks.clear();
    m_dragging = false;
    m_dragDeltaTicks = 0;

    if (waveProvider()) {
        m_wave = waveProvider()->clipWave(CLIP_WAVE_BUCKETS);
    }

    const INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    const Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const Measure* lastMeasure = score ? score->lastMeasureMM() : nullptr;

    m_scoreEndTick = lastMeasure ? lastMeasure->endTick().ticks() : 0;

    //! NOTE Where the measures and the beats fall is asked of the score once,
    //! here, and written down. Nothing is asked of it while the clip is being
    //! dragged, which is what keeps the drag up with the pointer.
    for (const Measure* measure = score ? score->firstMeasureMM() : nullptr; measure; measure = measure->nextMeasureMM()) {
        const int from = measure->tick().ticks();
        const int to = measure->endTick().ticks();

        m_barTicks.push_back(from);

        //! NOTE A beat is a beat of the time signature: an eighth in 6/8, say,
        //! and a quarter in 4/4. A measure that has not been given one is drawn
        //! in quarters, the way it is read.
        const Fraction timeSig = measure->timesig();
        const int ticksPerBeat = timeSig.denominator() > 1
                                 ? std::max(TICKS_PER_WHOLE_NOTE / timeSig.denominator(), MIN_TICKS_PER_BEAT)
                                 : Constants::DIVISION;

        for (int beat = from + ticksPerBeat; beat < to; beat += ticksPerBeat) {
            m_beatTicks.push_back(beat);
        }
    }

    if (!m_barTicks.empty()) {
        m_barTicks.push_back(m_scoreEndTick);
    }

    const int clipStart = m_wave.isValid() ? m_wave.startTick : 0;
    const int clipEnd = m_wave.isValid() ? m_wave.endTick : 0;

    //! NOTE The whole score is in view, and so is whatever of the clip has been
    //! dragged past either end of it, so that an end pushed out can be seen to
    //! have gone, and dragged back.
    m_viewFromTick = std::min(0, clipStart);
    m_viewToTick = std::max(m_scoreEndTick, clipEnd);

    if (m_viewToTick <= m_viewFromTick) {
        m_viewToTick = m_viewFromTick + Constants::DIVISION * 4;
    }

    emit hasClipChanged();
    update();
}

bool AudioClipTrackView::hasClip() const
{
    return m_wave.isValid();
}

double AudioClipTrackView::xForTick(double tick) const
{
    const double span = double(m_viewToTick - m_viewFromTick);
    if (span <= 0.0) {
        return 0.0;
    }

    return (tick - double(m_viewFromTick)) / span * double(width());
}

double AudioClipTrackView::tickForX(double x) const
{
    const double span = double(m_viewToTick - m_viewFromTick);
    if (width() <= 0.0) {
        return double(m_viewFromTick);
    }

    return double(m_viewFromTick) + (x / double(width())) * span;
}

float AudioClipTrackView::peakBetween(int firstTick, int lastTick) const
{
    const int span = m_wave.endTick - m_wave.startTick;
    if (span <= 0 || m_wave.peaks.empty()) {
        return 0.f;
    }

    //! NOTE The clip is drawn where the drag has put it, but its values are
    //! still read off the clip itself, so the drag is taken back out first.
    const double from = double(firstTick - m_dragDeltaTicks - m_wave.startTick) / double(span);
    const double to = double(lastTick - m_dragDeltaTicks - m_wave.startTick) / double(span);

    const size_t count = m_wave.peaks.size();
    const size_t first = std::clamp(size_t(std::floor(from * double(count))), size_t(0), count - 1);
    const size_t last = std::clamp(size_t(std::ceil(to * double(count))), first + 1, count);

    float peak = 0.f;
    for (size_t index = first; index < last; ++index) {
        peak = std::max(peak, m_wave.peaks[index]);
    }

    return peak;
}

void AudioClipTrackView::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(QRectF(0.0, 0.0, width(), height()), m_backgroundColor);

    if (m_barTicks.empty() && !m_wave.isValid()) {
        return;
    }

    //! NOTE The beats are drawn faintly and the measures plainly, and the wave
    //! over both: it is where the measures fall that has to be found at a glance
    //! when the clip is lined up.
    paintMarks(painter, m_beatTicks, m_beatLineColor, 1.0, height() * 0.30);
    paintMarks(painter, m_barTicks, m_barLineColor, 1.0, 0.0);

    if (!m_wave.isValid()) {
        return;
    }

    const double clipStart = double(m_wave.startTick) + double(m_dragDeltaTicks);
    const double clipEnd = double(m_wave.endTick) + double(m_dragDeltaTicks);

    const double left = std::max(xForTick(clipStart), 0.0);
    const double right = std::min(xForTick(clipEnd), double(width()));

    if (right > left) {
        QColor fill = m_clipColor;
        fill.setAlpha(m_dragging ? 90 : 60);

        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRect(QRectF(left, 0.0, right - left, height()));

        painter->setPen(QPen(m_clipColor, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(left, 0.0, right - left, height()));
    }

    paintWave(painter, clipStart, clipEnd);

    //! NOTE An end of the clip that has been slid off the score is neither drawn
    //! nor heard, so it is pointed out rather than left to be missed.
    if (clipStart < 0.0) {
        paintOffScoreMarker(painter, 1.0, true);
    }

    if (clipEnd > double(m_scoreEndTick)) {
        paintOffScoreMarker(painter, width() - 1.0, false);
    }
}

void AudioClipTrackView::paintMarks(QPainter* painter, const std::vector<int>& ticks,
                                    const QColor& color, double lineWidth, double inset) const
{
    //! NOTE A score long enough for its marks to fall closer together than they
    //! can be told apart is drawn without them, rather than as a solid band.
    const double leastGap = 3.0;

    QVector<QLineF> lines;
    lines.reserve(int(ticks.size()));

    double lastX = -1e9;

    for (int tick : ticks) {
        const double x = xForTick(double(tick));
        if (x < 0.0 || x > double(width()) || x - lastX < leastGap) {
            continue;
        }

        lines.append(QLineF(x, inset, x, height() - inset));
        lastX = x;
    }

    if (lines.isEmpty()) {
        return;
    }

    painter->setPen(QPen(color, lineWidth));
    painter->drawLines(lines);
}

void AudioClipTrackView::paintWave(QPainter* painter, double clipStart, double clipEnd) const
{
    const double centerY = height() * 0.5;
    const double maxAmplitude = height() * 0.44;
    const double minAmplitude = 0.5;

    const double from = std::max(xForTick(clipStart), 0.0);
    const double to = std::min(xForTick(clipEnd), double(width()));

    QVector<QLineF> lines;

    //! NOTE One point per pixel of the track, and each of them is the loudest of
    //! the stretch of the clip it stands for: a hit is then as tall as it is loud
    //! however far the track has been zoomed out, and it cannot fall between two
    //! points and be lost.
    for (double x = from; x < to; x += 1.0) {
        const int firstTick = int(std::floor(tickForX(x)));
        const int lastTick = int(std::ceil(tickForX(std::min(x + 1.0, to))));

        const float peak = peakBetween(firstTick, lastTick);
        const double amplitude = std::max(double(peak) * maxAmplitude, minAmplitude);

        lines.append(QLineF(x, centerY - amplitude, x, centerY + amplitude));
    }

    if (lines.isEmpty()) {
        return;
    }

    painter->setPen(QPen(m_waveColor, 1.0));
    painter->drawLines(lines);
}

void AudioClipTrackView::paintOffScoreMarker(QPainter* painter, double x, bool pointingLeft) const
{
    const double size = std::min(height() * 0.35, 10.0);
    const double centerY = height() * 0.5;
    const double base = pointingLeft ? x + size : x - size;

    const QPolygonF triangle = { QPointF(x, centerY),
                                 QPointF(base, centerY - size * 0.6),
                                 QPointF(base, centerY + size * 0.6) };

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_markerColor);
    painter->drawPolygon(triangle);
}

void AudioClipTrackView::mousePressEvent(QMouseEvent* event)
{
    if (!m_wave.isValid() || event->button() != Qt::LeftButton) {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    m_dragAnchorTick = int(std::lround(tickForX(event->position().x())));
    m_dragDeltaTicks = 0;

    setKeepMouseGrab(true);
    update();

    event->accept();
}

void AudioClipTrackView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QQuickPaintedItem::mouseMoveEvent(event);
        return;
    }

    //! NOTE The tick that was under the pointer stays under the pointer, and
    //! nothing is snapped: the clip is lined up by eye and by ear, not by a grid.
    const int delta = int(std::lround(tickForX(event->position().x()))) - m_dragAnchorTick;

    if (delta == m_dragDeltaTicks) {
        event->accept();
        return;
    }

    m_dragDeltaTicks = delta;
    update();

    event->accept();
}

void AudioClipTrackView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QQuickPaintedItem::mouseReleaseEvent(event);
        return;
    }

    const int tick = m_wave.startTick + m_dragDeltaTicks;

    m_dragging = false;
    m_dragDeltaTicks = 0;
    setKeepMouseGrab(false);

    //! NOTE The clip is only ever moved, never shortened: see the note in
    //! PlaybackCommandsController::moveAudioClip.
    if (actionsDispatcher()) {
        actionsDispatcher()->dispatch("move-audio-clip", muse::actions::ActionData::make_arg1<int>(tick));
    }

    update();

    event->accept();
}

void AudioClipTrackView::hoverMoveEvent(QHoverEvent* event)
{
    //! NOTE The pointer says what can be grabbed, which is the one thing that
    //! tells this track apart from the score above it.
    setCursor(m_wave.isValid() ? Qt::SizeHorCursor : Qt::ArrowCursor);

    QQuickPaintedItem::hoverMoveEvent(event);
}

void AudioClipTrackView::removeClip()
{
    if (actionsDispatcher()) {
        actionsDispatcher()->dispatch("detach-audio-clip");
    }
}

QColor AudioClipTrackView::backgroundColor() const
{
    return m_backgroundColor;
}

void AudioClipTrackView::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor == color) {
        return;
    }

    m_backgroundColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::clipColor() const
{
    return m_clipColor;
}

void AudioClipTrackView::setClipColor(const QColor& color)
{
    if (m_clipColor == color) {
        return;
    }

    m_clipColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::waveColor() const
{
    return m_waveColor;
}

void AudioClipTrackView::setWaveColor(const QColor& color)
{
    if (m_waveColor == color) {
        return;
    }

    m_waveColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::barLineColor() const
{
    return m_barLineColor;
}

void AudioClipTrackView::setBarLineColor(const QColor& color)
{
    if (m_barLineColor == color) {
        return;
    }

    m_barLineColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::beatLineColor() const
{
    return m_beatLineColor;
}

void AudioClipTrackView::setBeatLineColor(const QColor& color)
{
    if (m_beatLineColor == color) {
        return;
    }

    m_beatLineColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::markerColor() const
{
    return m_markerColor;
}

void AudioClipTrackView::setMarkerColor(const QColor& color)
{
    if (m_markerColor == color) {
        return;
    }

    m_markerColor = color;
    update();
    emit appearanceChanged();
}
