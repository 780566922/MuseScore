# MuseScore 音频轨 fork（feat/audio-track）

这是 MuseScore 的一个个人 fork，在官方 5.0 开发分支基础上加入了**原生音频轨**功能：
你可以把一段音频（伴奏、参考录音、鼓机导出等）直接拖进工程，作为一条独立的音轨，
按照谱面的拍号和速度对齐、显示波形，并拖动微调偏移。音频随谱面一起保存在 `.mscz` 里。

## 这个版本能做什么
- 把音频文件拖入工程，作为一条独立的音频轨（内嵌在 `.mscz` 中，随谱面一起保存）。
- 按谱面的拍号 + BPM 生成**静态时间网格**，把音频起点对齐到某一拍。
- 在底部独立的 **Audio 面板**里显示音频波形、小节线，以及每一拍的虚线格子。
- 在面板上**拖动音频做偏移**（只改起点，不裁切、不吸附）；松手才提交。
- 显示 / 隐藏音频轨；对齐模式下让谱面变淡，方便对照对齐。
- 支持格式：WAV / OGG / MP3 / FLAC / M4A(AAC)，按内容嗅探，不依赖文件后缀名。

## 怎么下载运行（仅 Apple Silicon Mac）
在 Release 里下载 `MuseScore-audio-track-arm64.dmg`，拖进「应用程序」即可。

- 本包是 **adhoc 签名**，没有苹果开发者公证。首次打开若被 Gatekeeper 拦截，
  请右键点击 →「打开」；或在终端执行：
  `xattr -dr com.apple.quarantine /Applications/mscore.app`
- **仅支持 Apple Silicon（arm64）芯片的 Mac**，Intel Mac 无法运行。
- 它的 bundle id 是 `org.musescore.MuseScoreStudio5Development`，
  与你机器上官方的 MuseScore 4 设置互不干扰，可共存。

## 已知限制
- 生成的 `.mscz` 是 **5.0 格式**，官方 MuseScore 4.x 打开会报「文件版本过新」。
  （让 4.x 也能打开的方案仍在评估中，尚未实现。）
- 仅 arm64；如需通用 / Intel 版本需另行编译。
- 这是个人 fork，与官方 MuseScore 项目无隶属关系。

## 自己编译
```bash
git clone --recurse-submodules -b feat/audio-track https://github.com/780566922/MuseScore.git
```
编译依赖 Qt 6 + CMake + Ninja，且必须加 `-DMUE_BUILD_MACOS_INTEGRATION=OFF`。
详见仓库内的 `musescore-build-setup.md`。

## 许可
与原 MuseScore 一致，遵循 GPL。

Assisted-by: WorkBuddy
