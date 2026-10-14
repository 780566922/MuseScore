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
#include <QElapsedTimer>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QTimer>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "actions/iactionsdispatcher.h"
#include "audio/common/audiotypes.h"
#include "context/iglobalcontext.h"

#include "notation/iaudioclipwaveprovider.h"
#include "notation/inotation_fwd.h"

namespace mu::notation {
//! The audio clip attached to the score, drawn as a track of its own below it.
//!
//! Unlike the score, which lays its notes out by how much room each of them
//! needs, this track lays the score out by tick: a beat is as wide as a beat,
//! wherever it falls. That is what makes it cheap to draw - nothing at all is
//! asked of the score while the clip is being dragged - and it is what lets the
//! wave, the measures and the beats be read off one another: they are all placed
//! by the one sum.
//!
//! The whole score fits the track to begin with, and it can be zoomed into:
//! the wheel (or a pinch on a trackpad) zooms about whatever is under the
//! pointer, the buttons in the corner zoom about the middle, and dragging the
//! track with nothing held down slides the view along the score. Holding Alt
//! (Option) while dragging slides the clip itself instead, which is the one
//! gesture that changes the score, and so is the one that asks to be meant.
//!
//! Dragging the clip only ever moves it, never shortens it: the part of it that
//! ends up before the beginning of the score, or past its end, is simply not
//! reached when the score is played back or exported, and comes back when the
//! clip is moved back over the score. A marker at either edge points out when
//! an end has been pushed out that way.
//!
//! Where the score has got to in being played is drawn as a line across the
//! track, at the tick the score is at, which is the same tick the cursor in the
//! score above is drawn at. The view follows it when it would otherwise be left
//! behind, so that zooming in does not mean losing sight of the playhead.
class AudioClipTrackView : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasClip READ hasClip NOTIFY hasClipChanged)
    Q_PROPERTY(int zoomPercentage READ zoomPercentage NOTIFY zoomChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor clipColor READ clipColor WRITE setClipColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor waveColor READ waveColor WRITE setWaveColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor barLineColor READ barLineColor WRITE setBarLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor beatLineColor READ beatLineColor WRITE setBeatLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor markerColor READ markerColor WRITE setMarkerColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor playbackCursorColor READ playbackCursorColor WRITE setPlaybackCursorColor NOTIFY appearanceChanged)

    muse::ContextInject<IAudioClipWaveProvider> waveProvider = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> actionsDispatcher = { this };

public:
    explicit AudioClipTrackView(QQuickItem* parent = nullptr);

    bool hasClip() const;

    //! How much of the score the track has been zoomed into, 100 being all of it.
    int zoomPercentage() const;

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
    QColor labelColor() const;
    void setLabelColor(const QColor& color);
    QColor playbackCursorColor() const;
    void setPlaybackCursorColor(const QColor& color);

    void paint(QPainter* painter) override;

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit();

    Q_INVOKABLE void removeClip();

signals:
    void hasClipChanged();
    void zoomChanged();
    void appearanceChanged();

protected:
    void componentComplete() override;

    bool event(QEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void wheelEvent(QWheelEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;

private:
    enum class DragMode {
        None,
        //! The clip is being slid along the score, which changes the score.
        MoveClip,
        //! The view is being slid along the score, which changes nothing.
        PanView
    };

    //! NOTE Everything the track draws is written down here, once, when the clip
    //! or the score changes. Nothing is asked of the score while the clip is being
    //! dragged, which is what keeps the drag up with the pointer.
    void rebuild();

    void applyZoom(double zoom, double anchorTick, double anchorRatio);
    void setViewRange(int fromTick, int toTick);

    void zoomAt(double x, double factor);
    void scrollByPixels(double deltaPixels);

    double contentSpan() const;
    double ticksPerPixel() const;

    double xForTick(double tick) const;
    double tickForX(double x) const;

    //! How far the clip swings, and how loud it is, between two of the track's
    //! ticks: `trough` comes back negative or nothing, the rest positive.
    void envelopeBetween(int firstTick, int lastTick, float& peak, float& trough, float& rms) const;

    void paintMarks(QPainter* painter, const std::vector<int>& ticks, const QColor& color,
                    double lineWidth, double inset, Qt::PenStyle style = Qt::SolidLine) const;
    void paintBarNumbers(QPainter* painter) const;
    void paintWave(QPainter* painter, double clipStart, double clipEnd);
    void buildWavePaths(double clipStart, double clipEnd);
    void paintOffScoreMarker(QPainter* painter, double x, bool pointingLeft) const;
    void paintPlaybackCursor(QPainter* painter) const;
    void paintViewOverview(QPainter* painter) const;

    void onPlayingChanged();
    void updatePlaybackCursorInterpolated();
    void syncPlaybackTick();
    void ensurePlaybackTickVisible(int tick);

    //! Where the score is played from is moved to the tick asked for, which is
    //! how a place picked out on the track becomes the place play starts at.
    void seekToTick(int tick);

    AudioClipWave m_wave;

    //! NOTE The tick the score runs over, with room either side of it for an end
    //! of the clip that has been pushed out, and the part of that which is in
    //! view: zooming and panning move the latter about inside the former.
    int m_scoreEndTick = 0;
    int m_contentFromTick = 0;
    int m_contentToTick = 0;
    int m_viewFromTick = 0;
    int m_viewToTick = 0;
    double m_zoom = 1.0;
    bool m_viewInitialised = false;

    std::vector<int> m_barTicks;
    std::vector<int> m_barNumbers;
    std::vector<int> m_beatTicks;

    QColor m_backgroundColor = QColor(38, 38, 38);
    QColor m_clipColor = QColor(26, 118, 214);
    QColor m_waveColor = QColor(96, 176, 255);
    QColor m_barLineColor = QColor(150, 150, 150);
    QColor m_beatLineColor = QColor(85, 85, 85);
    QColor m_markerColor = QColor(224, 96, 96);
    QColor m_labelColor = QColor(170, 170, 170);
    QColor m_playbackCursorColor = QColor(76, 175, 80);

    DragMode m_dragMode = DragMode::None;
    //! NOTE What the pointer is over when the drag begins is what it stays under,
    //! and the track is laid out by tick, so following it costs no more than
    //! taking the tick under the pointer now and taking the one it was over then.
    double m_dragStartX = 0.0;
    int m_dragStartViewFromTick = 0;
    int m_dragAnchorTick = 0;
    int m_dragDeltaTicks = 0;

    //! NOTE The wave is drawn once and kept, and only drawn again when something
    //! it depends on has changed: the playhead moves many times a second, and the
    //! wave does not, so it is not asked to keep up with it.
    bool m_waveDirty = true;
    //! The outline of the wave, and the body of it: the first is drawn from how
    //! far the clip swings either way, the second from how loud it is on the
    //! average, which is what makes a loud hit read as a solid one and a quiet
    //! stretch as a thin one.
    QPainterPath m_wavePath;
    QPainterPath m_waveBodyPath;

    QTimer m_playbackCursorTimer;
    QElapsedTimer m_elapsedTimer;
    muse::audio::secs_t m_lastPlaybackPosition = 0.0;
    qint64 m_lastPlaybackPositionUpdateTimeNs = 0;
    int m_playbackTick = 0;
    //! NOTE The view is carried along with the playhead unless the view has been
    //! slid somewhere of its own accord, in which case it is left where it was
    //! put until playing starts again.
    bool m_followPlayback = true;
    //! NOTE The score's own cursor is only drawn while the score is being played,
    //! but this one is where the score is to be played from, which is worth
    //! knowing whether it is being played or not: it stays put on being stopped,
    //! and it is moved by picking a place out on the track.
    bool m_playbackCursorVisible = true;
};
}
