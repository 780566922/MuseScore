/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) MuseScore Limited and others
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

#include "playbackcommandscontroller.h"

#include <vector>

#include "actions/actiontypes.h"
#include "rcommand/actiontocommand.h"
#include "rcommand/commandtypes.h"
#include "audio/common/audiotypes.h"

#include "../playbackcommands.h"

using namespace muse;
using namespace muse::rcommand;
using namespace muse::audio;
using namespace mu::playback;

static rcommand::CommandQuery mixerSectionToggle(const rcommand::Command& command, const actions::ActionData& args)
{
    rcommand::CommandQuery q(command);
    if (args.empty()) {
        return q;
    }

    std::string section = str_conv(static_cast<MixerSectionType>(args.arg<int>(0)));
    q.addParam("section", Val(section));
    return q;
}

void PlaybackCommandsController::init()
{
    registerCommand(PLAY_TOGGLE_COMMAND, &IPlaybackController::togglePlay);
    registerCommand(PLAY_COMMAND, &IPlaybackController::play, false);
    registerCommand(PLAY_SELECTION_COMMAND, &IPlaybackController::playFromSelection, false);
    registerCommand(PAUSE_COMMAND, &IPlaybackController::pause, false);
    registerCommand(PAUSE_AND_SELECT_COMMAND, &IPlaybackController::pause, true);
    registerCommand(STOP_COMMAND, &IPlaybackController::stop);
    registerCommand(REWIND_COMMAND, [this](const rcommand::Params& params) { return rewind(params); });
    registerCommand(SEEK_TO_COMMAND, [this](const rcommand::Params& params) { return seekTo(params); });
    registerCommand(LOOP_TOGGLE_COMMAND, &IPlaybackController::toggleLoopPlayback);
    registerCommand(LOOP_IN_COMMAND, &IPlaybackController::addLoopBoundary, LoopBoundaryType::LoopIn);
    registerCommand(LOOP_OUT_COMMAND, &IPlaybackController::addLoopBoundary, LoopBoundaryType::LoopOut);
    registerCommand(METRONOME_TOGGLE_COMMAND, &IPlaybackController::toggleMetronome);
    registerCommand(MIDI_TOGGLE_COMMAND, &IPlaybackController::toggleMidiInput);
    registerCommand(MIDI_INPUT_WRITTEN_PITCH_COMMAND, &IPlaybackController::setMidiUseWrittenPitch, true);
    registerCommand(MIDI_INPUT_SOUNDING_PITCH_COMMAND, &IPlaybackController::setMidiUseWrittenPitch, false);
    registerCommand(REPEATS_TOGGLE_COMMAND, &IPlaybackController::togglePlayRepeats);
    registerCommand(CHORDSYMBOLS_TOGGLE_COMMAND, &IPlaybackController::togglePlayChordSymbols);
    registerCommand(HEAR_PLAYBACK_WHEN_EDITING_TOGGLE_COMMAND, &IPlaybackController::toggleHearPlaybackWhenEditing);
    registerCommand(PAN_TOGGLE_COMMAND, &IPlaybackController::toggleAutomaticallyPan);
    registerCommand(COUNTIN_TOGGLE_COMMAND, &IPlaybackController::toggleCountIn);
    registerCommand(OPEN_PLAYBACK_SETUP_COMMAND, [this]() { return showPlaybackSetup(); });
    registerCommand(RELOAD_PLAYBACK_CACHE_COMMAND, &IPlaybackController::reloadPlaybackCache);

    registerCommand(ATTACH_AUDIO_CLIP_COMMAND, [this](const rcommand::Params& params) { return attachAudioClip(params); });
    registerCommand(DETACH_AUDIO_CLIP_COMMAND, [this]() { return detachAudioClip(); });
    registerCommand(MOVE_AUDIO_CLIP_COMMAND, [this](const rcommand::Params& params) { return moveAudioClip(params); });

    registerCommand(TOGGLE_MIXER_SECTION_COMMAND, [this](const rcommand::Params& params) { return toggleMixerSection(params); });
    registerCommand(TOGGLE_AUX_SEND_COMMAND, [this](const rcommand::Params& params) { return toggleAuxSend(params); });
    registerCommand(TOGGLE_AUX_CHANNEL_COMMAND, [this](const rcommand::Params& params) { return toggleAuxChannel(params); });

    // compat
    {
        static std::vector<ActionToCommand> actionToCommand = {
            { "play", PLAY_TOGGLE_COMMAND, {} },
            { "play-from-selection", PLAY_SELECTION_COMMAND, {} },
            { "pause", PAUSE_COMMAND, {} },
            { "pause-and-select", PAUSE_AND_SELECT_COMMAND, {} },
            { "stop", STOP_COMMAND, {} },
            { "rewind", REWIND_COMMAND, {} },
            { "seek-to", SEEK_TO_COMMAND, make_conv({ { "position", param<double> } }) },
            { "loop", LOOP_TOGGLE_COMMAND, {} },
            { "loop-in", LOOP_IN_COMMAND, {} },
            { "loop-out", LOOP_OUT_COMMAND, {} },
            { "metronome", METRONOME_TOGGLE_COMMAND, {} },
            { "playback-setup", OPEN_PLAYBACK_SETUP_COMMAND, {} },
            { "midi-on", MIDI_TOGGLE_COMMAND, {} },
            { "midi-input-written-pitch", MIDI_INPUT_WRITTEN_PITCH_COMMAND, {} },
            { "midi-input-sounding-pitch", MIDI_INPUT_SOUNDING_PITCH_COMMAND, {} },
            { "repeats", REPEATS_TOGGLE_COMMAND, {} },
            { "play-chord-symbols", CHORDSYMBOLS_TOGGLE_COMMAND, {} },
            { "toggle-hear-playback-when-editing", HEAR_PLAYBACK_WHEN_EDITING_TOGGLE_COMMAND, {} },
            { "pan", PAN_TOGGLE_COMMAND, {} },
            { "countin", COUNTIN_TOGGLE_COMMAND, {} },
            { "reload-playback-cache", RELOAD_PLAYBACK_CACHE_COMMAND, {} },
            { "clear-online-sounds-cache", CLEAR_ONLINESOUNDS_CACHE_COMMAND, {} },
            { "toggle-mixer-section", TOGGLE_MIXER_SECTION_COMMAND, mixerSectionToggle },
            { "toggle-aux-send", TOGGLE_AUX_SEND_COMMAND, make_conv({ { "auxsend-index", param<int> } }) },
            { "toggle-aux-channel", TOGGLE_AUX_CHANNEL_COMMAND, make_conv({ { "auxchannel-index", param<int> } }) },
            { "attach-audio-clip", ATTACH_AUDIO_CLIP_COMMAND, make_conv({ { "path", param<io::path_t> } }) },
            { "detach-audio-clip", DETACH_AUDIO_CLIP_COMMAND, {} },
            { "move-audio-clip", MOVE_AUDIO_CLIP_COMMAND, make_conv({ { "tick", param<int> } }) },
        };

        registerActionToCommand(this, actionToCommand, dispatcher(), actionsDispatcher());
    }
}

//! Formats the decoder is able to make sense of; used both to pick a file and
//! to tell whether a file dropped onto the score is one we can play.
static const std::vector<std::string> audioClipFileFilters = {
    "*.wav", "*.wave", "*.ogg", "*.oga", "*.flac", "*.m4a", "*.mp3"
};

muse::Ret PlaybackCommandsController::attachAudioClip(const muse::rcommand::Params& params)
{
    io::path_t path;

    auto search = params.find("path");
    if (search != params.end() && !search->second.toString().empty()) {
        path = io::path_t(search->second.toString());
    } else {
        path = interactive()->selectOpeningFileSync(muse::trc("playback", "Attach an audio file"), "",
                                                    audioClipFileFilters);
    }

    if (path.empty()) {
        return muse::make_ret(muse::Ret::Code::Cancel);
    }

    return playbackController()->attachAudioClip(path);
}

muse::Ret PlaybackCommandsController::detachAudioClip()
{
    playbackController()->detachAudioClip();
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::moveAudioClip(const muse::rcommand::Params& params)
{
    auto search = params.find("tick");
    if (search == params.end()) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    playbackController()->moveAudioClip(search->second.toInt());
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::rewind(const muse::rcommand::Params& params)
{
    double secs = params.at("position").toDouble();
    return playbackController()->rewind(secs_t(secs));
}

//! NOTE Unlike rewinding, which is asked for by the player's own bar, this is
//! asked for by a place in the score having been picked out - a stretch of the
//! audio track say - and it is not played from there unless play is asked for.
muse::Ret PlaybackCommandsController::seekTo(const muse::rcommand::Params& params)
{
    auto search = params.find("position");
    if (search == params.end()) {
        return muse::make_ret(muse::Ret::Code::UnknownError);
    }

    return playbackController()->rewind(secs_t(search->second.toDouble()));
}

muse::Ret PlaybackCommandsController::showPlaybackSetup()
{
    interactive()->open("musescore://playback/soundprofiles");
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleMixerSection(const muse::rcommand::Params& params)
{
    MixerSectionType section = str_conv(params.at("section").toString(), MixerSectionType::Unknown);
    if (section == MixerSectionType::Unknown) {
        return make_ret(Ret::Code::BadArgs);
    }

    bool visible = configuration()->isMixerSectionVisible(section);
    configuration()->setMixerSectionVisible(section, !visible);
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleAuxSend(const muse::rcommand::Params& params)
{
    if (!params.contains("auxsend-index")) {
        return make_ret(Ret::Code::BadArgs);
    }

    aux_channel_idx_t auxSendIndex = static_cast<aux_channel_idx_t>(params.at("auxsend-index").toInt());
    bool visible = configuration()->isAuxSendVisible(auxSendIndex);
    configuration()->setAuxSendVisible(auxSendIndex, !visible);
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleAuxChannel(const muse::rcommand::Params& params)
{
    if (!params.contains("auxchannel-index")) {
        return make_ret(Ret::Code::BadArgs);
    }

    aux_channel_idx_t auxChannelIndex = static_cast<aux_channel_idx_t>(params.at("auxchannel-index").toInt());
    bool visible = configuration()->isAuxChannelVisible(auxChannelIndex);
    configuration()->setAuxChannelVisible(auxChannelIndex, !visible);
    return make_ok();
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, const std::function<Ret()>& handler)
{
    dispatcher()->onRequest(this, command, [this, command, handler]() {
        if (!commandsState()->commandState(command).enabled) {
            return muse::make_ret(Ret::Code::NotSupported);
        }

        return handler();
    });
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command,
                                                 const std::function<muse::Ret(const muse::rcommand::Params&)>& handler)
{
    dispatcher()->onRequest(this, command, [this, command, handler](const muse::rcommand::Params& params) {
        if (!commandsState()->commandState(command).enabled) {
            return muse::make_ret(Ret::Code::NotSupported);
        }

        return handler(params);
    });
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, Ret (IPlaybackController::* handler)())
{
    registerCommand(command, [this, handler]()
    {
        return (playbackController().get()->*handler)();
    });
}

template<typename P1>
void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, Ret (IPlaybackController::* handler)(P1), P1 p1)
{
    registerCommand(command, [this, handler, p1]()
    {
        return (playbackController().get()->*handler)(p1);
    });
}
