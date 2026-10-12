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
    }

    StyledTextLabel {
        anchors.centerIn: parent
        visible: !track.hasClip
        text: qsTrc("notation", "No audio. Drag an audio file onto the score to attach one.")
        color: ui.theme.fontPrimaryColor
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
