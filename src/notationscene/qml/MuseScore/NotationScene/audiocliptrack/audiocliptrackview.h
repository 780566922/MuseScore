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

#pragma once

#include <vector>

#include <QColor>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"

#include "notation/iaudioclipwaveprovider.h"
#include "notation/inotation_fwd.h"

namespace mu::notation {
//! The audio clip attached to the score, drawn as a track of its own below it.
//!
//! Unlike the score, which lays its notes out by how much room each of them
//! needs, this track lays the score out by tick: a beat is as wide as a beat,
//! wherever it falls, and the whole score is in front of you at once. That is
//! what makes it cheap to draw - nothing at all is asked of the score while the
//! clip is being dragged - and it is what lets the wave, the measures and the
//! beats be read off one another: they are all placed by the one sum.
//!
//! Dragging the track slides the clip along the score. The clip is only ever
//! moved, never shortened: the part of it that ends up before the beginning of
//! the score, or past its end, is simply not reached when the score is played
//! back or exported, and comes back when the clip is moved back over the score.
//! A marker at either edge points out when an end has been pushed out that way.
class AudioClipTrackView : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasClip READ hasClip NOTIFY hasClipChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor clipColor READ clipColor WRITE setClipColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor waveColor READ waveColor WRITE setWaveColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor barLineColor READ barLineColor WRITE setBarLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor beatLineColor READ beatLineColor WRITE setBeatLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor markerColor READ markerColor WRITE setMarkerColor NOTIFY appearanceChanged)

    muse::ContextInject<IAudioClipWaveProvider> waveProvider = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> actionsDispatcher = { this };

public:
    explicit AudioClipTrackView(QQuickItem* parent = nullptr);

    bool hasClip() const;

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);
    QColor clipColor() const;
    void setClipColor(const QColor& color);
    QColor waveColor() const;
    void setWaveColor(const QColor& color);
    QColor barLineColor() const;
    void setBarLineColor(const QColor& color);
    QColor beatLineColor() const;
    void setBeatLineColor(const QColor& color);
    QColor markerColor() const;
    void setMarkerColor(const QColor& color);

    void paint(QPainter* painter) override;

    Q_INVOKABLE void removeClip();

signals:
    void hasClipChanged();
    void appearanceChanged();

protected:
    void componentComplete() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;

private:
    //! NOTE Everything the track draws is written down here, once, when the clip
    //! or the score changes. Nothing is asked of the score while the clip is being
    //! dragged, which is what keeps the drag up with the pointer.
    void rebuild();

    double xForTick(double tick) const;
    double tickForX(double x) const;

    //! The loudest the clip gets between two of the track's ticks.
    float peakBetween(int firstTick, int lastTick) const;

    void paintMarks(QPainter* painter, const std::vector<int>& ticks, const QColor& color,
                    double lineWidth, double inset) const;
    void paintWave(QPainter* painter, double clipStart, double clipEnd) const;
    void paintOffScoreMarker(QPainter* painter, double x, bool pointingLeft) const;

    AudioClipWave m_wave;

    int m_scoreEndTick = 0;
    int m_viewFromTick = 0;
    int m_viewToTick = 0;
    std::vector<int> m_barTicks;
    std::vector<int> m_beatTicks;

    QColor m_backgroundColor = QColor(38, 38, 38);
    QColor m_clipColor = QColor(26, 118, 214);
    QColor m_waveColor = QColor(96, 176, 255);
    QColor m_barLineColor = QColor(150, 150, 150);
    QColor m_beatLineColor = QColor(85, 85, 85);
    QColor m_markerColor = QColor(224, 96, 96);

    bool m_dragging = false;
    //! NOTE What the pointer is over when the drag begins is what it stays under,
    //! and the track is laid out by tick, so following it costs no more than
    //! taking the tick under the pointer now and taking the one it was over then.
    int m_dragAnchorTick = 0;
    int m_dragDeltaTicks = 0;
};
}
