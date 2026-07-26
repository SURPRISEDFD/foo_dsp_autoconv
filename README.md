# foo_dsp_autoconv — 采样率自适应校准卷积 DSP

foobar2000 DSP 组件（C++ / 官方 SDK v2）。播放时实时检测音频流采样率，递归扫描用户配置的文件夹（含全部子文件夹），把**文件名中包含当前采样率**的所有校准 WAV 作为卷积核（脉冲响应）**串联**应用到播放流上，做实时分块 FFT 卷积，并自动匹配处理前后的电平。

DSP 显示名称：**Auto Calibration Convolver**，组件文件名：`foo_dsp_autoconv.dll`。

## 功能

- 实时监测流采样率（44100 / 48000 / 88200 / 96000 / 192000 …），采样率、声道数或声道布局变化时自动重载对应的校准文件
- 递归扫描校准文件夹及其**所有子文件夹**中的 `*.wav`（扩展名大小写不敏感）；文件名中包含当前采样率（如 `EQ_96000.wav`、`sub\room-96000-v2.wav`）即被采用。匹配按**数字边界**判断——采样率两侧不能紧邻其他数字，因此 8000 Hz 流不会误匹配 `Cal_48000.wav`。只匹配文件名本身，不匹配文件夹名
- 匹配到多个文件时**全部串联生效**（级联卷积）：加载时用 FFT 把它们预先合成为一个组合脉冲响应，与逐级串联在数学上完全等价、与文件顺序无关，运行时开销与单文件相同。单个文件损坏或实际采样率不符时仅跳过该文件（控制台注明原因），其余文件继续生效；全部不可用才透传
- 找不到文件 / 采样率不匹配 / 文件损坏：在 foobar2000 控制台（View → Console）输出明确日志，音频**原样透传**，绝不中断播放
- 自动电平匹配：作用于**串联合成后的整条链**，按能量归一（白噪声/宽带增益 ≈ 0 dB），保留多声道间的相对平衡；另有 ±24 dB 手动增益微调
- **找不到精确采样率文件时自动重采样（可开关，默认开）**：从所有文件名里提取出现过的采样率，按对数距离选择最接近流采样率的一组文件，用 [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)（Voxengo 出品的高质量 SRC，MIT）把**脉冲响应**重采样到流采样率后再串联——重采样的永远是 IR，播放音频本身绝不被重采样。关闭开关即恢复严格匹配行为。杜绝“冷门采样率没有对应文件就没有校准”的情况
- **FFT 尺寸自适应（可开关，默认开）**：块大小取不小于合成 IR 长度的最小 2 的幂，并钳制在 512～32768（如 512 / 1024 / 2048 / 4096 / … / 32768），短 IR 用小块、长 IR 用大块以降低每采样 CPU 开销；**构造上只可能是 2 的幂**，绝不会出现 16385 这类导致 FFT 退化的尺寸。关闭则固定 4096。更大的块只增加延迟，而延迟由 `get_latency()` 上报、foobar2000 自动补偿
- 均匀分块 overlap-save FFT 卷积（自带 FFT，实时路径无第三方依赖），长 IR 也能低开销实时处理
- 正确的首尾处理：内部缓冲量通过 `get_latency()` 上报；播放结束或格式切换时自动排空完整卷积尾音；seek 时清空历史避免串音；跨曲目保持状态以兼容无缝播放
- 配置界面（DSP 设置弹窗，纯 Win32，无 WTL/ATL 依赖）：总开关、校准文件夹（带浏览按钮）、自动电平开关、手动增益、最近采样率重采样开关、FFT 自适应开关

## 目录结构

```
.
├── .github/workflows/build.yml   # CI：下载 SDK → MSBuild x86+x64 → Artifact / Release
├── .gitignore
├── LICENSE
├── README.md
├── tools/
│   ├── get_sdk.ps1               # 下载并解压官方 SDK 到 ./SDK（本地和 CI 共用）
│   └── get_r8brain.ps1           # 下载 r8brain-free-src 到 ./third_party/r8brain（同上）
└── src/
    ├── foo_dsp_autoconv.vcxproj  # 项目文件（通过 ProjectReference 引用 SDK 三个工程）
    ├── fb2k_sdk.h                # SDK 头文件统一入口（SDK 路径变动只改这里）
    ├── main.cpp                  # 组件版本声明 / DLL 名校验
    ├── dsp_autoconv.cpp          # DSP 主体：格式检测、重载、透传、尾音、日志
    ├── preset.h / preset.cpp     # 配置结构与 dsp_preset 二进制序列化（兼容旧版含模板的预设）
    ├── ir_chain.h / .cpp         # 递归扫描 + 采样率匹配（含最近采样率回退）+ 多文件级联合成
    ├── ir_resampler.h / .cpp     # r8brain IR 重采样封装（仅加载时调用，不在音频路径）
    ├── convolver.h / .cpp        # 均匀分块 overlap-save 卷积引擎
    ├── fft.h                     # 自带迭代基-2 FFT（header-only）
    ├── wav_loader.h / .cpp       # 独立 RIFF/WAVE 解析（PCM 16/24/32、float 32/64、EXTENSIBLE）
    ├── config_dialog.h / .cpp    # Win32 模态配置对话框
    ├── resource.h
    └── foo_dsp_autoconv.rc
```

刻意不提供 `.sln`：SDK 各版本工程 GUID 会变，直接构建 `src\foo_dsp_autoconv.vcxproj` 即可，MSBuild / VS 会通过 ProjectReference 自动拉起并链接 `pfc`、`foobar2000_SDK`、`foobar2000_component_client` 三个 SDK 工程。

## 本地编译（Visual Studio 2022）

```powershell
# 1. 仓库根目录下载 SDK（首次一次即可；URL 见下方“SDK 链接”注意事项）
#    脚本会顺带把 SDK 工程的 PlatformToolset 从 v142 补丁为 v143——SDK 默认
#    以 /GL (LTCG) 编译，/GL 目标文件跨编译器版本不兼容，不统一工具集会在
#    链接阶段报 C1047。
powershell -ExecutionPolicy Bypass -File tools\get_sdk.ps1

# 1b. 下载 r8brain-free-src 重采样库到 third_party\r8brain（首次一次即可）
powershell -ExecutionPolicy Bypass -File tools\get_r8brain.ps1

# 2a. 命令行编译
msbuild src\foo_dsp_autoconv.vcxproj /m /p:Configuration=Release /p:Platform=x64
msbuild src\foo_dsp_autoconv.vcxproj /m /p:Configuration=Release /p:Platform=Win32

# 2b. 或直接用 VS 打开 src\foo_dsp_autoconv.vcxproj，选 Release + 平台后生成
```

产物位于 `_bin\Release\<平台>\foo_dsp_autoconv.dll`。

## CI（GitHub Actions）

`.github/workflows/build.yml`：

- 触发：push 到 `main` / `master`、推送 `v*` 标签、PR、手动触发
- `windows-latest` 上按 `Win32` / `x64` 矩阵：`tools/get_sdk.ps1` 下载官方 SDK、`tools/get_r8brain.ps1` 下载 r8brain-free-src（各自带 actions/cache 缓存）→ `setup-msbuild` → `msbuild ... /p:Configuration=Release`
- 两个 DLL 分别作为 Artifact 上传；`package` 任务再组装成标准 `foo_dsp_autoconv.fb2k-component`（x86 DLL 在包根目录，x64 DLL 在 `x64\` 子目录）
- 推送 `v*` 标签时自动创建/附加 GitHub Release

### ⚠️ SDK 链接必须核对

官方 SDK 每次发布都会更换文件名（当前为 `SDK-2025-03-07.7z`，`.7z` 格式）。`build.yml` 顶部的 `FB2K_SDK_URL` 与 `get_sdk.ps1` 的默认 URL 已指向该版本；官方发布新 SDK 后，请到 <https://www.foobar2000.org/SDK> 复制新链接替换（`.7z` 与 `.zip` 均可，脚本按扩展名自动识别）。`.7z` 解压依赖 7-Zip：GitHub Actions 的 windows runner 已预装，本地若未安装请从 <https://www.7-zip.org/> 获取。foobar2000.org 的直链偶尔有防盗链/UA 限制，若 CI 下载失败，可把 SDK 压缩包存到你自己的 Release/对象存储再指向它。

## 安装与使用

1. 安装 CI 产出的 `foo_dsp_autoconv.fb2k-component`（双击或拖入 Preferences → Components），或手动把对应架构的 DLL 放进 `profile\user-components\foo_dsp_autoconv\`
2. Preferences → Playback → DSP Manager，把 **Auto Calibration Convolver** 加入激活链，点 “...” 打开配置
3. 设置校准文件夹，确认 “Enable processing” 勾选。文件命名规则：文件名中含有采样率即可，例如 `EQ_96000.wav`、`96000 room.wav`、`headphone-96000-L+R.wav`；放在任意层级的子文件夹里都会被找到。同一采样率有多个文件时会全部串联应用（如耳机 EQ + 房间校正各一个文件）。链上限 16 个文件，合成后总长 ≤ 4,194,304 taps。若当前流采样率没有任何文件命中且重采样开关开启，会自动改用文件名里最接近的采样率那组文件并把 IR 重采样到流采样率（控制台会注明所用采样率）
4. 播放任意曲目，打开 View → Console 查看加载/透传日志

### 校准文件要求

- RIFF/WAVE：PCM 16/24/32-bit 或 IEEE float 32/64-bit（含 WAVE_FORMAT_EXTENSIBLE）
- 文件头的实际采样率应与文件名标注一致。重采样开关开启（默认）时，头采样率与流不一致的文件会被 r8brain 重采样到流采样率后照常使用（控制台注明 `resampled X -> Y Hz`）；关闭时此类文件按旧行为跳过并提示
- 声道数为 1（应用到所有声道）或与流一致（逐声道卷积）；其他情况使用第 1 声道并提示
- 长度上限 4,194,304 帧（约 96 kHz 下 43 秒），远超常见房间校正 IR

### 电平匹配说明

勾选 Auto level match 时，插件对**串联合成后的组合脉冲响应**按各声道均方能量的平均值做单一全局缩放，使白噪声（宽带）增益为 0 dB——普通音乐的整体响度基本不变，同时不破坏 IR 各声道间的相对差异。如需以峰值频响归一等其他口径，改 `dsp_autoconv.cpp` 中的 `compute_gain()` 即可。手动增益（±24 dB）叠加其上，实际应用的总增益会打印到控制台。

### 延迟与首尾

分块卷积需要先积满一个块（自适应模式 512～32768 采样，取决于 IR 长度；固定模式 4096），内部缓冲量实时通过 `get_latency()` 报告给 foobar2000；播放结束（`on_endofplayback`）或流格式变化时自动补零排空“缓冲余量 + IR 长度 − 1”的完整尾音，不丢任何音频；seek（`flush()`）时清空历史，杜绝跨越 seek 的串音；跨曲目不清状态，保证无缝播放连续。

## 兼容性与已知注意事项（请务必阅读）

- 全部源码已在 GitHub Actions 中对着官方 **SDK-2025-03-07**（MSVC 14.44 / v143）编译通过，本组件的 API 调用（`dsp_impl_base`、`audio_chunk`、`dsp_factory_t`、`console`、pfc 字符串等）零错误零警告。若未来 SDK 有签名/路径变动，集中修改点：`src/fb2k_sdk.h`（SDK 头路径）与 `src/dsp_autoconv.cpp`（`audio_chunk` 存取调用）。
- **运行库必须与 SDK 一致（/MD）**：SDK 以动态 CRT 编译，本工程 Release 已设为 `MultiThreadedDLL`（Debug 为 `MultiThreadedDebugDLL`）。切勿改回静态 `/MT`——链接会立刻报出成片的 `LNK2038 'RuntimeLibrary'` 不匹配和 `__imp_*` 未解析符号。VC++ 运行库随 foobar2000 官方安装包分发，用户无需单独安装。
- 关于 “foobar2000 v1.5+”：**最低支持版本由所用 SDK 决定**。较新的 SDK 通常要求 fb2k ≥ 1.6.x（x64 要求 2.0+）。若必须严格支持 1.5，请把 `FB2K_SDK_URL` 换成对应年代的旧版 SDK；本代码只用长期稳定 API，配合旧 SDK 编译通常无需改动。
- IR 文件在播放线程内同步加载（仅在采样率/声道变化时发生一次），常规大小的 WAV 为毫秒级；极端超大文件请留意首块的短暂加载时间。
- r8brain-free-src 在构建时由脚本从官方仓库获取（默认跟随 master，未 vendor 进本仓库；要固定版本可在 `build.yml` 的 `R8BRAIN_URL` 里指向某个 commit 的归档）。该库官方声明兼容 MSVC，纯 C++ 无额外依赖。
- `shared` 导入库在不同 SDK 版本中命名不同（`shared.lib` vs `shared-Win32.lib`），`get_sdk.ps1` 已自动做兼容改名。

## License

MIT，见 [LICENSE](LICENSE)。foobar2000 SDK 本身遵循其自带许可条款（SDK 不随本仓库分发）。

重采样功能使用 [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)（MIT，构建时获取，不随本仓库分发）。按其作者要求致谢：**“Sample rate converter designed by Aleksey Vaneev of Voxengo.”**
