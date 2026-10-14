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
#include <QWheelEvent>
#include <QtGui/qevent.h>

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/types/constants.h"
#include "engraving/types/fraction.h"

#include "context/iplaybackstate.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationplayback.h"
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

//! The least of the score that can be zoomed into, and the most of it that a
//! turn of the wheel or the buttons zooms by.
static constexpr int MIN_ZOOMED_TICKS = Constants::DIVISION * 2;
static constexpr double ZOOM_STEP = 1.5;

AudioClipTrackView::AudioClipTrackView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFillColor(Qt::transparent);

    m_playbackCursorTimer.setInterval(23); // ~43 fps, as the score's own cursor
    connect(&m_playbackCursorTimer, &QTimer::timeout, this, &AudioClipTrackView::updatePlaybackCursorInterpolated);
}

void AudioClipTrackView::componentComplete()
{
    QQuickPaintedItem::componentComplete();

    if (globalContext()) {
        globalContext()->currentNotationChanged().onNotify(this, [this]() {
            rebuild();
        });

        if (auto state = globalContext()->playbackState()) {
            state->playbackPositionChanged().onReceive(this, [this](const muse::audio::secs_t secs) {
                m_lastPlaybackPosition = secs;
                m_lastPlaybackPositionUpdateTimeNs = m_elapsedTimer.nsecsElapsed();

                //! NOTE Nothing is carrying the playhead along by the clock while
                //! the score is stopped, so a seek from anywhere else - the score
                //! above, or this track - is taken as it comes.
                if (!m_playbackCursorTimer.isActive()) {
                    syncPlaybackTick();
                }
            }, Asyncable::Mode::SetReplace);

            state->playbackStatusChanged().onReceive(this, [this](const muse::audio::PlaybackStatus) {
                onPlayingChanged();
            }, Asyncable::Mode::SetReplace);
        }
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
    m_barNumbers.clear();
    m_beatTicks.clear();
    m_dragMode = DragMode::None;
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

        //! NOTE The number written at a bar line is the one the score itself
        //! writes over that measure, so the two can be told off against one
        //! another.
        m_barNumbers.push_back(measure->measureNumber() + 1);

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
        m_barNumbers.push_back(m_barNumbers.back() + 1);
    }

    const int clipStart = m_wave.isValid() ? m_wave.startTick : 0;
    const int clipEnd = m_wave.isValid() ? m_wave.endTick : 0;

    //! NOTE The whole score is the track's content, and so is whatever of the
    //! clip has been dragged past either end of it, so that an end pushed out can
    //! be seen to have gone, and dragged back.
    m_contentFromTick = std::min(0, clipStart);
    m_contentToTick = std::max(m_scoreEndTick, clipEnd);

    if (m_contentToTick <= m_contentFromTick) {
        m_contentToTick = m_contentFromTick + Constants::DIVISION * 4;
    }

    //! NOTE Zooming is kept across a rebuild - the clip being moved, or the score
    //! being edited, does not throw away where the track had been zoomed to - and
    //! it is the middle of what was in view that is held still.
    const bool firstTime = !m_viewInitialised;
    const double zoom = firstTime ? 1.0 : m_zoom;
    const double middle = firstTime
                          ? double(m_contentFromTick + m_contentToTick) * 0.5
                          : double(m_viewFromTick + m_viewToTick) * 0.5;

    applyZoom(zoom, middle, 0.5);
    m_viewInitialised = true;

    m_waveDirty = true;

    syncPlaybackTick();

    emit hasClipChanged();
    update();
}

bool AudioClipTrackView::hasClip() const
{
    return m_wave.isValid();
}

int AudioClipTrackView::zoomPercentage() const
{
    return int(std::lround(m_zoom * 100.0));
}

double AudioClipTrackView::contentSpan() const
{
    return double(m_contentToTick - m_contentFromTick);
}

double AudioClipTrackView::ticksPerPixel() const
{
    if (width() <= 0.0) {
        return 0.0;
    }

    return double(m_viewToTick - m_viewFromTick) / double(width());
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
    return double(m_viewFromTick) + x * ticksPerPixel();
}

void AudioClipTrackView::setViewRange(int fromTick, int toTick)
{
    const double span = contentSpan();
    if (span <= 0.0) {
        return;
    }

    //! NOTE The view is never made narrower than a couple of beats, where there
    //! would be nothing left to read, nor wider than the score, where there would
    //! be nothing to gain, and it is never slid off either end of it.
    const double minSpan = std::min(span, double(MIN_ZOOMED_TICKS));
    const double viewSpan = std::clamp(double(toTick - fromTick), minSpan, span);
    const double from = std::clamp(double(fromTick), double(m_contentFromTick), double(m_contentToTick) - viewSpan);

    const int newFrom = int(std::lround(from));
    const int newTo = int(std::lround(from + viewSpan));

    if (newFrom == m_viewFromTick && newTo == m_viewToTick) {
        return;
    }

    m_viewFromTick = newFrom;
    m_viewToTick = newTo;

    const double newZoom = span / viewSpan;
    const bool zoomChangedNow = std::abs(newZoom - m_zoom) > 0.001;

    m_zoom = newZoom;
    m_waveDirty = true;

    if (zoomChangedNow) {
        emit zoomChanged();
    }

    update();
}

void AudioClipTrackView::applyZoom(double zoom, double anchorTick, double anchorRatio)
{
    const double span = contentSpan();
    if (span <= 0.0 || width() <= 0.0) {
        return;
    }

    const double minSpan = std::min(span, double(MIN_ZOOMED_TICKS));
    const double viewSpan = std::clamp(span / std::max(zoom, 0.0001), minSpan, span);
    const double ratio = std::clamp(anchorRatio, 0.0, 1.0);
    const double from = anchorTick - ratio * viewSpan;

    setViewRange(int(std::lround(from)), int(std::lround(from + viewSpan)));
}

void AudioClipTrackView::zoomAt(double x, double factor)
{
    //! NOTE Whatever is under the pointer - or under the middle of the track, for
    //! the buttons - stays under it, which is the one thing that makes a zoom in
    //! or out by the wheel something that can be aimed.
    applyZoom(m_zoom * factor, tickForX(x), x / std::max(width(), 1.0));
}

void AudioClipTrackView::scrollByPixels(double deltaPixels)
{
    const double deltaTicks = deltaPixels * ticksPerPixel();

    if (deltaTicks == 0.0) {
        return;
    }

    setViewRange(int(std::lround(double(m_viewFromTick) + deltaTicks)),
                 int(std::lround(double(m_viewToTick) + deltaTicks)));
}

void AudioClipTrackView::zoomIn()
{
    zoomAt(width() * 0.5, ZOOM_STEP);
}

void AudioClipTrackView::zoomOut()
{
    zoomAt(width() * 0.5, 1.0 / ZOOM_STEP);
}

void AudioClipTrackView::zoomToFit()
{
    setViewRange(m_contentFromTick, m_contentToTick);
}

void AudioClipTrackView::envelopeBetween(int firstTick, int lastTick, float& peak, float& trough, float& rms) const
{
    const int span = m_wave.endTick - m_wave.startTick;
    if (span <= 0 || m_wave.peaks.empty()) {
        return;
    }

    //! NOTE The clip is drawn where the drag has put it, but its values are
    //! still read off the clip itself, so the drag is taken back out first.
    const double from = double(firstTick - m_dragDeltaTicks - m_wave.startTick) / double(span);
    const double to = double(lastTick - m_dragDeltaTicks - m_wave.startTick) / double(span);

    const size_t count = m_wave.peaks.size();
    const size_t first = std::clamp(size_t(std::floor(from * double(count))), size_t(0), count - 1);
    const size_t last = std::clamp(size_t(std::ceil(to * double(count))), first + 1, count);

    double sumOfSquares = 0.0;
    size_t values = 0;

    for (size_t index = first; index < last; ++index) {
        peak = std::max(peak, m_wave.peaks[index]);

        if (index < m_wave.troughs.size()) {
            trough = std::min(trough, m_wave.troughs[index]);
        }

        //! NOTE Loudness is not a greatest-of, the way a swing is: however many
        //! stretches of the clip one pixel of the track stands for, it is as loud
        //! as the square mean of them all.
        if (index < m_wave.rmses.size()) {
            sumOfSquares += double(m_wave.rmses[index]) * double(m_wave.rmses[index]);
            ++values;
        }
    }

    rms = values > 0 ? float(std::sqrt(sumOfSquares / double(values))) : 0.f;
}

void AudioClipTrackView::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(QRectF(0.0, 0.0, width(), height()), m_backgroundColor);

    if (m_barTicks.empty() && !m_wave.isValid()) {
        return;
    }

    //! NOTE The beats are drawn as a dotted grid over the whole height of the
    //! track, so that each beat is a cell the wave can be read off, and the
    //! measures plainly over that: it is where the measures fall that has to be
    //! found at a glance when the clip is lined up.
    paintMarks(painter, m_beatTicks, m_beatLineColor, 1.0, 0.0, Qt::DotLine);
    paintMarks(painter, m_barTicks, m_barLineColor, 1.0, 0.0);
    paintBarNumbers(painter);

    if (!m_wave.isValid()) {
        paintPlaybackCursor(painter);
        paintViewOverview(painter);
        return;
    }

    const double clipStart = double(m_wave.startTick) + double(m_dragDeltaTicks);
    const double clipEnd = double(m_wave.endTick) + double(m_dragDeltaTicks);

    const double left = std::max(xForTick(clipStart), 0.0);
    const double right = std::min(xForTick(clipEnd), double(width()));

    if (right > left) {
        QColor fill = m_clipColor;
        fill.setAlpha(m_dragMode == DragMode::MoveClip ? 90 : 60);

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

    paintPlaybackCursor(painter);
    paintViewOverview(painter);
}

void AudioClipTrackView::paintMarks(QPainter* painter, const std::vector<int>& ticks,
                                    const QColor& color, double lineWidth, double inset, Qt::PenStyle style) const
{
    //! NOTE A score long enough, or a zoom far enough out, for the marks to fall
    //! closer together than they can be told apart is drawn without them, rather
    //! than as a solid band.
    const double leastGap = 3.0;

    //! NOTE The marks are written down in the order they fall, so the ones off
    //! either side of the view can be skipped over rather than gone through.
    const auto first = std::lower_bound(ticks.begin(), ticks.end(), m_viewFromTick);

    QVector<QLineF> lines;
    lines.reserve(int(std::distance(first, ticks.end())));

    double lastX = -1e9;

    for (auto it = first; it != ticks.end(); ++it) {
        const double x = xForTick(double(*it));

        if (x > double(width())) {
            break;
        }

        if (x < 0.0 || x - lastX < leastGap) {
            continue;
        }

        lines.append(QLineF(x, inset, x, height() - inset));
        lastX = x;
    }

    if (lines.isEmpty()) {
        return;
    }

    QPen pen(color, lineWidth);
    pen.setStyle(style);
    painter->setPen(pen);
    painter->drawLines(lines);
}

void AudioClipTrackView::paintBarNumbers(QPainter* painter) const
{
    if (m_barTicks.empty() || width() < 40.0) {
        return;
    }

    //! NOTE Too many numbers to tell apart is worse than none, so they are only
    //! written where there is room between one bar line and the next to read them.
    const double leastGap = 24.0;

    const auto first = std::lower_bound(m_barTicks.begin(), m_barTicks.end(), m_viewFromTick);
    const size_t firstIndex = size_t(std::distance(m_barTicks.begin(), first));

    QFont font = painter->font();
    font.setPixelSize(9);
    painter->setFont(font);
    painter->setPen(m_labelColor);

    double lastX = -1e9;

    for (size_t index = firstIndex; index < m_barTicks.size(); ++index) {
        const double x = xForTick(double(m_barTicks[index]));

        if (x > double(width())) {
            break;
        }

        if (x < 0.0 || x - lastX < leastGap) {
            continue;
        }

        lastX = x;

        painter->drawText(QRectF(x + 2.0, 1.0, 40.0, height() * 0.5),
                          Qt::AlignLeft | Qt::AlignTop, QString::number(m_barNumbers[index]));
    }
}

void AudioClipTrackView::buildWavePaths(double clipStart, double clipEnd)
{
    m_wavePath = QPainterPath();
    m_waveBodyPath = QPainterPath();

    if (!m_wave.isValid() || width() <= 0.0) {
        return;
    }

    const double centerY = height() * 0.5;
    const double maxAmplitude = height() * 0.44;
    const double minAmplitude = 0.5;

    const double from = std::max(xForTick(clipStart), 0.0);
    const double to = std::min(xForTick(clipEnd), double(width()));

    const int points = int(std::ceil(to - from));
    if (points < 1) {
        return;
    }

    //! NOTE One point per pixel of the track, each of them the furthest the
    //! clip swings either way over the stretch it stands for: a hit is then as
    //! tall as it is loud however far the track has been zoomed out, and it
    //! cannot fall between two points and be lost.
    QVector<QPointF> tops(points);
    QVector<QPointF> bottoms(points);
    QVector<QPointF> bodyTops(points);
    QVector<QPointF> bodyBottoms(points);

    for (int point = 0; point < points; ++point) {
        const double x = from + double(point);
        const int firstTick = int(std::floor(tickForX(x)));
        const int lastTick = int(std::ceil(tickForX(std::min(x + 1.0, to))));

        float peak = 0.f;
        float trough = 0.f;
        float rms = 0.f;
        envelopeBetween(firstTick, lastTick, peak, trough, rms);

        const double up = std::max(double(peak) * maxAmplitude, minAmplitude);
        const double down = std::max(double(-trough) * maxAmplitude, minAmplitude);
        const double body = std::max(double(rms) * maxAmplitude, minAmplitude);

        tops[point] = QPointF(x, centerY - up);
        bottoms[point] = QPointF(x, centerY + down);
        bodyTops[point] = QPointF(x, centerY - body);
        bodyBottoms[point] = QPointF(x, centerY + body);
    }

    //! NOTE The wave is drawn as one closed shape rather than as a line per
    //! pixel: an outline run along the tops and back along the bottoms, which
    //! is what lets it be filled and smoothed, so that it reads as a wave and
    //! not as a row of blocks.
    m_wavePath.moveTo(tops.first());
    for (int point = 1; point < points; ++point) {
        m_wavePath.lineTo(tops[point]);
    }
    for (int point = points - 1; point >= 0; --point) {
        m_wavePath.lineTo(bottoms[point]);
    }
    m_wavePath.closeSubpath();

    m_waveBodyPath.moveTo(bodyTops.first());
    for (int point = 1; point < points; ++point) {
        m_waveBodyPath.lineTo(bodyTops[point]);
    }
    for (int point = points - 1; point >= 0; --point) {
        m_waveBodyPath.lineTo(bodyBottoms[point]);
    }
    m_waveBodyPath.closeSubpath();
}

void AudioClipTrackView::paintWave(QPainter* painter, double clipStart, double clipEnd)
{
    //! NOTE The playhead asks to be drawn many times a second, and the wave does
    //! not change at all while it moves, so the wave is drawn once and kept until
    //! the view, the clip or the track itself has changed under it.
    if (m_waveDirty) {
        buildWavePaths(clipStart, clipEnd);
        m_waveDirty = false;
    }

    if (m_wavePath.isEmpty()) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    QColor outline = m_waveColor;
    outline.setAlpha(110);
    painter->setBrush(outline);
    painter->drawPath(m_wavePath);

    QColor body = m_waveColor;
    body.setAlpha(230);
    painter->setBrush(body);
    painter->drawPath(m_waveBodyPath);

    painter->setRenderHint(QPainter::Antialiasing, false);
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

void AudioClipTrackView::paintPlaybackCursor(QPainter* painter) const
{
    if (!m_playbackCursorVisible) {
        return;
    }

    const double x = xForTick(double(m_playbackTick));

    if (x < 0.0 || x > double(width())) {
        return;
    }

    //! NOTE The score above draws its cursor at the tick the score has got to in
    //! being played, and so does this: the two are at the same tick, and the two
    //! lines fall one above the other for as much as the score's own layout,
    //! which gives a beat room by how much room its notes need, lets them.
    painter->setPen(QPen(m_playbackCursorColor, 1.5));
    painter->drawLine(QLineF(x, 0.0, x, height()));

    const double size = 4.0;
    const QPolygonF head = { QPointF(x - size, 0.0), QPointF(x + size, 0.0), QPointF(x, size * 1.4) };

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_playbackCursorColor);
    painter->drawPolygon(head);
}

void AudioClipTrackView::paintViewOverview(QPainter* painter) const
{
    //! NOTE Once the track is zoomed in, part of the score is off the side of it,
    //! and which part is worth knowing without having to zoom back out to see.
    if (m_zoom <= 1.01) {
        return;
    }

    const double span = contentSpan();
    if (span <= 0.0) {
        return;
    }

    const double height_ = 3.0;
    const double top = height() - height_;
    const double from = double(m_viewFromTick - m_contentFromTick) / span * double(width());
    const double to = double(m_viewToTick - m_contentFromTick) / span * double(width());

    painter->setPen(Qt::NoPen);

    painter->setBrush(m_beatLineColor);
    painter->drawRect(QRectF(0.0, top, width(), height_));

    painter->setBrush(m_barLineColor);
    painter->drawRect(QRectF(from, top, std::max(to - from, 3.0), height_));
}

void AudioClipTrackView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }

    //! NOTE Moving the clip is the one gesture here that changes the score, so it
    //! is the one that asks to be meant: it takes Alt (Option) held down. A drag
    //! with nothing held down slides the view over the score, which is how the
    //! track is moved along once it has been zoomed into.
    if (m_wave.isValid() && event->modifiers().testFlag(Qt::AltModifier)) {
        m_dragMode = DragMode::MoveClip;
        m_dragAnchorTick = int(std::lround(tickForX(event->position().x())));
        m_dragDeltaTicks = 0;
    } else {
        m_dragMode = DragMode::PanView;
        m_dragStartX = event->position().x();
        m_dragStartViewFromTick = m_viewFromTick;
        m_followPlayback = false;
        setCursor(Qt::ClosedHandCursor);
    }

    setKeepMouseGrab(true);
    update();

    event->accept();
}

void AudioClipTrackView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragMode == DragMode::None) {
        QQuickPaintedItem::mouseMoveEvent(event);
        return;
    }

    if (m_dragMode == DragMode::MoveClip) {
        //! NOTE The tick that was under the pointer stays under the pointer, and
        //! nothing is snapped: the clip is lined up by eye and by ear, not by a
        //! grid.
        const int delta = int(std::lround(tickForX(event->position().x()))) - m_dragAnchorTick;

        if (delta == m_dragDeltaTicks) {
            event->accept();
            return;
        }

        m_dragDeltaTicks = delta;
        m_waveDirty = true;
    } else {
        const double deltaTicks = (m_dragStartX - event->position().x()) * ticksPerPixel();
        const int span = m_viewToTick - m_viewFromTick;
        const int from = int(std::lround(double(m_dragStartViewFromTick) + deltaTicks));

        setViewRange(from, from + span);
    }

    update();

    event->accept();
}

void AudioClipTrackView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragMode == DragMode::None) {
        QQuickPaintedItem::mouseReleaseEvent(event);
        return;
    }

    const bool movedClip = m_dragMode == DragMode::MoveClip;

    //! NOTE A drag that went nowhere is a click, and a click on the track picks
    //! out where the score is to be played from, which is the whole point of
    //! being able to point at a stretch of the audio and say "from here".
    const bool pickedAPlace = m_dragMode == DragMode::PanView
                              && std::abs(event->position().x() - m_dragStartX) <= 3.0;

    const int tick = m_wave.startTick + m_dragDeltaTicks;

    m_dragMode = DragMode::None;
    m_dragDeltaTicks = 0;
    m_waveDirty = true;

    setKeepMouseGrab(false);
    setCursor(m_wave.isValid() && event->modifiers().testFlag(Qt::AltModifier) ? Qt::SizeHorCursor : Qt::ArrowCursor);

    //! NOTE The clip is only ever moved, never shortened: see the note in
    //! PlaybackCommandsController::moveAudioClip.
    if (movedClip && actionsDispatcher()) {
        actionsDispatcher()->dispatch("move-audio-clip", muse::actions::ActionData::make_arg1<int>(tick));
    } else if (pickedAPlace) {
        seekToTick(int(std::lround(tickForX(event->position().x()))));
    }

    update();

    event->accept();
}

void AudioClipTrackView::mouseDoubleClickEvent(QMouseEvent* event)
{
    //! NOTE The whole score in one gesture, which is what is wanted after having
    //! been zoomed in to line a hit up.
    zoomToFit();

    event->accept();
}

void AudioClipTrackView::hoverMoveEvent(QHoverEvent* event)
{
    if (m_dragMode == DragMode::None) {
        //! NOTE The pointer says what can be grabbed: the clip, when Alt is held,
        //! and otherwise the view, which can only be slid along the score once
        //! there is more of the score than there is track to show it in.
        if (m_wave.isValid() && event->modifiers().testFlag(Qt::AltModifier)) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(m_zoom > 1.01 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
    }

    QQuickPaintedItem::hoverMoveEvent(event);
}

void AudioClipTrackView::wheelEvent(QWheelEvent* event)
{
    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();

    //! NOTE A trackpad says how far its two fingers have moved, and a mouse how
    //! far its wheel has turned, and the two are told apart by that: a trackpad
    //! slid two fingers over the track slides the view - pinching is what zooms
    //! it, and comes in as a gesture of its own - while a wheel turned over it
    //! zooms. A wheel held with Shift slides the view as well, whichever it is.
    const bool fromTrackpad = !pixelDelta.isNull();
    const bool sideways = angleDelta.y() == 0 && angleDelta.x() != 0;
    const bool heldWithShift = event->modifiers().testFlag(Qt::ShiftModifier);

    if (sideways || heldWithShift || fromTrackpad) {
        const double delta = fromTrackpad
                             ? double(pixelDelta.x() != 0 ? pixelDelta.x() : pixelDelta.y())
                             : double(angleDelta.x() != 0 ? angleDelta.x() : angleDelta.y()) / 4.0;

        m_followPlayback = false;
        scrollByPixels(-delta);
    } else {
        const double steps = double(angleDelta.y()) / 120.0;
        zoomAt(event->position().x(), std::pow(ZOOM_STEP, steps));
    }

    event->accept();
}

bool AudioClipTrackView::event(QEvent* event)
{
    //! NOTE Pinching two fingers on a trackpad is not a wheel turned, but a
    //! gesture, and is what zooms the track on one.
    if (event->type() == QEvent::NativeGesture) {
        const QNativeGestureEvent* gesture = static_cast<const QNativeGestureEvent*>(event);

        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const QPointF local = mapFromGlobal(gesture->globalPosition());
            const double x = std::clamp(local.x(), 0.0, double(width()));

            zoomAt(x, 1.0 + gesture->value() * 2.0);
            event->accept();
            return true;
        }

        if (gesture->gestureType() == Qt::SmartZoomNativeGesture) {
            zoomToFit();
            event->accept();
            return true;
        }
    }

    return QQuickPaintedItem::event(event);
}

void AudioClipTrackView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);

    //! NOTE The wave is drawn a point at a time, and a point per pixel, so a
    //! track that has been made wider or narrower has to have it drawn again.
    m_waveDirty = true;
}

void AudioClipTrackView::onPlayingChanged()
{
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    const bool playing = state && state->isPlaying();

    //! NOTE The playhead is not hidden on being stopped: unlike the score's own
    //! cursor, it says where playing is to start from, which is as much wanted
    //! while the score is stopped as while it is being played.
    m_playbackCursorVisible = true;
    m_followPlayback = true;

    if (playing) {
        m_lastPlaybackPosition = state->playbackPosition();
        m_lastPlaybackPositionUpdateTimeNs = m_elapsedTimer.nsecsElapsed();

        updatePlaybackCursorInterpolated();
        m_playbackCursorTimer.start();
    } else {
        m_playbackCursorTimer.stop();
        syncPlaybackTick();
        update();
    }
}

void AudioClipTrackView::syncPlaybackTick()
{
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;

    const INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    const INotationPlaybackPtr playback = notation && notation->masterNotation()
                                          ? notation->masterNotation()->playback()
                                          : nullptr;

    if (!state || !playback) {
        return;
    }

    m_playbackTick = playback->secToTick(state->playbackPosition());
}

void AudioClipTrackView::seekToTick(int tick)
{
    const INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    const INotationPlaybackPtr playback = notation && notation->masterNotation()
                                          ? notation->masterNotation()->playback()
                                          : nullptr;

    //! NOTE The track is laid out by the tick the score is written in, but where
    //! playing is moved to is a time, and repeats mean the two are not the same
    //! sum: the score's own playback model is asked to take one into the other.
    if (playback) {
        const muse::RetVal<muse::midi::tick_t> playedTick = playback->playPositionTickByRawTick(tick);
        const double secs = playedTick.ret ? double(playback->playedTickToSec(playedTick.val)) : 0.0;

        m_playbackTick = tick;
        m_followPlayback = true;
        update();

        if (actionsDispatcher()) {
            actionsDispatcher()->dispatch("seek-to", muse::actions::ActionData::make_arg1<double>(secs));
        }
    }
}

void AudioClipTrackView::updatePlaybackCursorInterpolated()
{
    const INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    const INotationPlaybackPtr playback = notation && notation->masterNotation()
                                          ? notation->masterNotation()->playback()
                                          : nullptr;

    if (!playback) {
        return;
    }

    //! NOTE Where the score is in being played is told in seconds, and only now
    //! and then, so it is carried on by the clock in between, the way the score's
    //! own cursor is, rather than stepping from one telling to the next.
    const muse::audio::secs_t deltaSecs = (m_elapsedTimer.nsecsElapsed() - m_lastPlaybackPositionUpdateTimeNs) / 1e9;
    const int tick = playback->secToTick(m_lastPlaybackPosition + deltaSecs);

    if (tick == m_playbackTick) {
        return;
    }

    m_playbackTick = tick;

    ensurePlaybackTickVisible(tick);

    update();
}

void AudioClipTrackView::ensurePlaybackTickVisible(int tick)
{
    if (!m_followPlayback) {
        return;
    }

    const double span = double(m_viewToTick - m_viewFromTick);
    if (span <= 0.0) {
        return;
    }

    //! NOTE Zoomed in, the score goes by faster than the track can show it, so
    //! the view is carried along with the playhead rather than letting it walk off
    //! the side: it is brought back a little way in from the left when it comes
    //! near either edge.
    const double margin = span * 0.08;

    if (double(tick) >= double(m_viewFromTick) + margin && double(tick) <= double(m_viewToTick) - margin) {
        return;
    }

    const double from = double(tick) - span * 0.15;

    setViewRange(int(std::lround(from)), int(std::lround(from + span)));
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

QColor AudioClipTrackView::labelColor() const
{
    return m_labelColor;
}

void AudioClipTrackView::setLabelColor(const QColor& color)
{
    if (m_labelColor == color) {
        return;
    }

    m_labelColor = color;
    update();
    emit appearanceChanged();
}

QColor AudioClipTrackView::playbackCursorColor() const
{
    return m_playbackCursorColor;
}

void AudioClipTrackView::setPlaybackCursorColor(const QColor& color)
{
    if (m_playbackCursorColor == color) {
        return;
    }

    m_playbackCursorColor = color;
    update();
    emit appearanceChanged();
}
