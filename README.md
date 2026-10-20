> ⚠️ **这是带「原生音频轨」功能的 MuseScore fork（非官方版本）。**
> *This is an unofficial MuseScore fork that adds a native **audio track** feature.*
>
> 相比官方 MuseScore，本 fork 多了一件事：可以把音频文件直接拖进乐谱，成为一条独立的**音频轨**——
> 按拍号 + BPM 做成静态对齐网格、在底部 Audio 面板画出波形与每拍虚线、可拖动做偏移，支持 **WAV / MP3 / Ogg / FLAC / M4A(AAC)**。
>
> - 📦 **已经编好、可直接用的成品（仅 Apple Silicon）**：见 [Releases · audio-track-v1](https://github.com/780566922/MuseScore/releases/tag/audio-track-v1)（下载 DMG → 拖进「应用程序」即可）
> - 🌿 功能代码在分支 [`feat/audio-track`](https://github.com/780566922/MuseScore/tree/feat/audio-track)；音频解码放在子模块 `muse_framework` 的 `feat/audio-file-source`
> - 📖 功能与从源码编译的说明：[AUDIO_TRACK.md](https://github.com/780566922/MuseScore/blob/feat/audio-track/AUDIO_TRACK.md)
>
> ⚠️ **已知限制**：生成的 `.mscz` 为 5.0 格式，官方 MuseScore 4.x 打不开；目前仅提供 arm64（Apple Silicon）版本。

---

# ![MuseScore Studio](share/icons/musescore_logo_full.png)

Music notation and composition software

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Coverage](https://s3.us-east-1.amazonaws.com/extensions.musescore.org/test/code_coverage/coverage_badge.svg?)](https://github.com/musescore/MuseScore/actions/workflows/check_unit_tests.yml)

MuseScore Studio is an open source and free music notation software. For support, contribution, and bug reports visit MuseScore.org. Fork and make pull requests!

## Features

- WYSIWYG design, notes are entered on a "virtual notepaper"
- TrueType font(s) for printing & display allows for high quality scaling to all sizes
- Easy & fast note entry
- Many editing functions
- MusicXML import/export
- MIDI (SMF) import/export
- MEI import/export
- MuseData import
- MIDI input for note entry
- Integrated sequencer and software synthesizer to play the score
- Print or create PDF files

## More info

- [MuseScore Studio Homepage](https://musescore.org)
- [MuseScore Studio Git workflow instructions](https://musescore.org/en/developers-handbook/git-workflow)
- [How to compile MuseScore Studio?](https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment)

## License

MuseScore Studio is licensed under GPL version 3.0. See [license file](https://github.com/musescore/MuseScore/blob/master/LICENSE.txt) in the same directory.

## Packages

See [Code Structure on Wiki](https://github.com/musescore/MuseScore/wiki/CodeStructure)

## Building

**Read the [Compilation section](https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment) of the [MuseScore Wiki](https://github.com/musescore/MuseScore/wiki) for a complete build walkthrough and a list of dependencies.**

### Getting sources

If using git to download repo of entire code history, type:

    git clone  --recurse-submodules https://github.com/musescore/MuseScore.git
    cd MuseScore

(The `--recurse-submodules` ensures that the [`muse_framework`](https://github.com/musescore/muse_framework) git submodule is checked out into the `muse/` subdirectory.)

Otherwise, you can just download the latest source release tarball from the [Releases page](https://github.com/musescore/MuseScore/releases), and then from your download directory type:

    tar xzf MuseScore-x.x.x.tar.gz
    cd MuseScore-x.x.x

### Release Build

To compile MuseScore Studio for release, type:

    cmake -P build.cmake -DCMAKE_BUILD_TYPE=Release

On MacOS, append `-G Ninja` in order to use the `ninja` build tool (which you may need to install), since this
is required to compile Swift components.

If something goes wrong, append the word "clean" to the above command to delete the build subdirectory:

    cmake -P build.cmake -DCMAKE_BUILD_TYPE=Release clean

Then try running the first command again.

### Running

To start MuseScore Studio, type:

    cmake -P build.cmake -DCMAKE_BUILD_TYPE=Release run

Or run the compiled executable directly.

### Debug Build

A debug version can be built and run by replacing `-DCMAKE_BUILD_TYPE=Release`
with `-DCMAKE_BUILD_TYPE=Debug` in the above commands.

If you omit the `-DCMAKE_BUILD_TYPE` option entirely then `RelWithDebInfo` is
used by default, as it provides a useful compromise between Release and Debug.

### Testing

See the [Unit tests section](https://github.com/musescore/MuseScore/wiki/Unit-tests) of the [MuseScore Studio Wiki](https://github.com/musescore/MuseScore/wiki) for instructions on how to run the test suite.

### Code Formatting

Run `./hooks/install.sh` to install a pre-commit hook that will format your staged files. Requires that you install `uncrustify`.

If you have problems, please report them. To uninstall, run `./hooks/uninstall.sh`.
