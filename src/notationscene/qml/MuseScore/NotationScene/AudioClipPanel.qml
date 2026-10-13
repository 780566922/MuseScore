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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

Item {
    id: root

    property NavigationSection navigationSection: null
    property int contentNavigationPanelOrderStart: 1

    anchors.fill: parent

    AudioClipTrackView {
        id: track

        anchors.fill: parent

        backgroundColor: ui.theme.backgroundPrimaryColor
        clipColor: ui.theme.accentColor
        waveColor: ui.theme.fontPrimaryColor
        barLineColor: ui.theme.strokeColor
        beatLineColor: ui.theme.buttonColor
        markerColor: ui.theme.accentColor
        labelColor: ui.theme.fontPrimaryColor
        playbackCursorColor: ui.theme.accentColor
    }

    StyledTextLabel {
        anchors.centerIn: parent
        visible: !track.hasClip
        text: qsTrc("notation", "No audio. Drag an audio file onto the score to attach one.")
        color: ui.theme.fontPrimaryColor
    }

    //! NOTE Zooming is done with the wheel, or by pinching on a trackpad, but a
    //! panel can swallow a wheel, so there is a way to do it by hand as well.
    Rectangle {
        id: zoomControls

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4

        width: zoomControlsRow.implicitWidth + 8
        height: 28

        visible: track.hasClip

        color: ui.theme.backgroundPrimaryColor
        opacity: 0.85
        radius: 3

        RowLayout {
            id: zoomControlsRow

            anchors.centerIn: parent
            spacing: 2

            FlatButton {
                icon: IconCode.ZOOM_OUT
                iconFont: ui.theme.toolbarIconsFont

                width: height
                height: 24
                transparent: true

                toolTipTitle: qsTrc("notation", "Zoom out")
                accessible.name: qsTrc("notation", "Zoom out")

                onClicked: {
                    track.zoomOut()
                }
            }

            FlatButton {
                icon: IconCode.ZOOM_IN
                iconFont: ui.theme.toolbarIconsFont

                width: height
                height: 24
                transparent: true

                toolTipTitle: qsTrc("notation", "Zoom in")
                accessible.name: qsTrc("notation", "Zoom in")

                onClicked: {
                    track.zoomIn()
                }
            }

            FlatButton {
                text: qsTrc("notation", "Fit")

                height: 24
                transparent: true

                toolTipTitle: qsTrc("notation", "Zoom to fit the whole score")
                accessible.name: qsTrc("notation", "Zoom to fit the whole score")

                onClicked: {
                    track.zoomToFit()
                }
            }

            StyledTextLabel {
                Layout.leftMargin: 2

                text: track.zoomPercentage + "%"
                color: ui.theme.fontPrimaryColor
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton

        onClicked: function(mouse) {
            if (!track.hasClip) {
                return
            }

            contextMenuModel.loadAudioClipItems()
            contextMenuLoader.show(Qt.point(mouse.x, mouse.y), contextMenuModel.items)
        }
    }

    NotationContextMenuModel {
        id: contextMenuModel
    }

    ContextMenuLoader {
        id: contextMenuLoader

        onHandleMenuItem: function(itemId) {
            contextMenuModel.handleMenuItem(itemId)
        }
    }
}
