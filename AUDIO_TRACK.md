# MuseScore 音频轨 fork（feat/audio-track）

这是 MuseScore 的一个个人 fork，在官方 5.0 开发分支基础上加入了**原生音频轨**功能：
你可以把一段音频（伴奏、参考录音、鼓机导出等）直接拖进工程，作为一条独立的音轨，
按照谱面的拍号和速度对齐、显示波形，并拖动微调偏移。音频随谱面一起保存在 `.mscz` 里。

> 本仓库代码是**跨平台**的（Windows / Linux / macOS 都能编译），音频解码等新增代码里
> **没有任何 macOS 专属依赖**。目前 Release 里只有 macOS arm64 成品，只是因为作者在 Mac 上构建。
> 想要 Windows / Linux / Intel Mac 版本，请按下方「自己编译」一节自行构建。

## 这个版本能做什么
- 把音频文件拖入工程，作为一条独立的音频轨（内嵌在 `.mscz` 中，随谱面一起保存）。
- 按谱面的拍号 + BPM 生成**静态时间网格**，把音频起点对齐到某一拍。
- 在底部独立的 **Audio 面板**里显示音频波形、小节线，以及每一拍的虚线格子。
- 在面板上**拖动音频做偏移**（只改起点，不裁切、不吸附）；松手才提交。
- 显示 / 隐藏音频轨；对齐模式下让谱面变淡，方便对照对齐。
- 支持格式：WAV / OGG / MP3 / FLAC / M4A(AAC)，按内容嗅探，不依赖文件后缀名。

## 下载运行（我们发布的成品：macOS arm64）
在 Release 里下载 `MuseScore-audio-track-arm64.dmg`，拖进「应用程序」即可。

- 本包是 **adhoc 签名**，没有苹果开发者公证。首次打开若被 Gatekeeper 拦截，
  请右键点击 →「打开」；或在终端执行：
  `xattr -dr com.apple.quarantine /Applications/mscore.app`
- 这个成品**仅支持 Apple Silicon（arm64）芯片的 Mac**，Intel Mac 无法运行
  （这是成品包的限制，不是代码的限制——见下方「自己编译」）。
- 它的 bundle id 是 `org.musescore.MuseScoreStudio5Development`，
  与你机器上官方的 MuseScore 4 设置互不干扰，可共存。

## 已知限制
- 生成的 `.mscz` 是 **5.0 格式**，官方 MuseScore 4.x 打开会报「文件版本过新」。
  （让 4.x 也能打开的方案仍在评估中，尚未实现。）
- 目前只发布了 macOS arm64 成品（因为只在 Mac 上构建过）。**代码本身跨平台、无 Mac 专属依赖**，
  Windows / Linux / Intel Mac 版本请自行编译。
- Windows / Linux 版尚未经过实测，理论上可编译，但可能需要针对平台的小修。
- 这是个人 fork，与官方 MuseScore 项目无隶属关系。

## 自己编译

### 通用准备
1. 克隆仓库（`--recurse-submodules` 必须加，音频解码在 `muse_framework` 子模块里）：
   ```bash
   git clone --recurse-submodules -b feat/audio-track https://github.com/780566922/MuseScore.git
   cd MuseScore
   ```
2. 安装通用工具：**CMake**、**Ninja**、**Qt 6**（5.0 分支要求 Qt 6，本仓库基于 Qt 6.11）。
3. 第三方库（libFLAC、libfdk-aac、libmp3lame、libopus 等）由 `muse_deps` 子模块按平台提供，
   无需单独安装。

### macOS（作者实测通过：Apple Silicon，Qt 6.11.2）
```bash
brew install qt cmake ninja          # Qt 6.11.x
mkdir build.release && cd build.release
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DMUE_BUILD_MACOS_INTEGRATION=OFF ..
ninja
ninja install                         # 产物在 applebuild/mscore.app
```
> `-DMUE_BUILD_MACOS_INTEGRATION=OFF` 只在 macOS 需要（关掉系统集成），其他平台不加这个参数。
> 想打出可分发 `.dmg`：`macdeployqt mscore.app -dmg`（把 Qt 等依赖打进包）。

### Intel Mac
步骤与 macOS 完全相同，只是用 Intel 原生工具链构建即可（同样是 `-DMUE_BUILD_MACOS_INTEGRATION=OFF`）。
作者没有 Intel 机器，未实测，但代码无架构专属依赖，应可编出。

### Windows（本 fork 未实测，请以官方 MuseScore Windows 编译指南为准）
- 安装：**Visual Studio 2022**（勾选"使用 C++ 的桌面开发"）、**Qt 6**（Qt 在线安装器）、CMake、Ninja。
- 用 VS 的"x64 Native Tools 命令提示符"或 PowerShell，`CMAKE_PREFIX_PATH` 指向你的 Qt 6 安装目录，
  然后 `cmake -GNinja ..` + `ninja`（**不要**加 `-DMUE_BUILD_MACOS_INTEGRATION=OFF`，那是 macOS 专用）。
- 打包：用 `windeployqt` 把 Qt DLL 收进 exe 同目录，即可得到可运行文件夹 / 安装包。
- 提示：本 fork 未在 Windows 上编译过，若在第三方库链接等环节遇到问题，属于"未实测"的正常范围。

### Linux（本 fork 未实测，请以官方 MuseScore Linux 编译指南为准）
- 安装 Qt6 开发包、CMake、Ninja、GCC/Clang（各发行版包名不同，参考官方 Linux 构建文档）。
- 同样 `cmake -GNinja .. && ninja`（不加 macOS 专用参数）。
- 运行时可用 in-place 运行，或 `linuxdeploy` 打包成 AppImage。

### 更权威的步骤
各平台详细命令随 MuseScore 版本可能变化，以上为本 fork 的最小说明；
权威、最新的编译步骤请以官方 MuseScore 开发手册为准：
https://musescore.org/en/developers-handbook

## 许可
与原 MuseScore 一致，遵循 GPL。

Assisted-by: WorkBuddy
