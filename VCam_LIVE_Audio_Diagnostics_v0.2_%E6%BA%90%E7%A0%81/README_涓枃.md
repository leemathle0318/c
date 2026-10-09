# VCam LIVE Audio Diagnostics v0.2（第一阶段）

**先看这句话：这是可编译的测试源码，不是“OBS 音频已经接入 TikTok LIVE”的修复版。** 目前没有 TikTok LIVE 的真实音频调用日志，也拿不到 VCam 原版的完整源码，无法可靠地把 OBS 音频接入 TikTok LIVE。插件默认纯观察：不会更改、静音、录制、上传或转发麦克风 PCM，也不会修改现有 `VCam 1.1.0`。

## 适配范围

- iPhone 8 Plus（A11）与 iPhone SE 第二代（A13）。
- iOS 15.0–16.6.1，Dopamine **rootless** 作为诊断模块的目标范围（未逐机实测）。
- TikTok 美区一般使用 Bundle ID `com.zhiliaoapp.musically`。请确认实际安装的 App Bundle ID，否则诊断模块不会加载。
- 此模块只注入 TikTok **App 进程**，不注入 `mediaserverd`。对于普通 App 进程，arm64 构建可以在 A11/A13 使用；**如果后续为 SE2 修改 iOS 系统进程，需要单独处理 arm64e、指针认证和系统版本差异。**
- 原版 VCam README 只声称 iOS 16 测试；不能推断 VCam 1.1.0 原版本身支持 iOS 15。

## 为什么做诊断

已知用户场景：iPhone 相机录像和 TikTok 普通录像都能获得 OBS 原声，TikTok LIVE 画面正常，但观众端只听到手机实体麦克风。原版 VCam 1.1.0 DEB 的 `VCam.dylib` 注入 `mediaserverd`，可识别音频 Hook 为 `BWAudioConverterNode renderSampleBuffer:forInput:`。这说明 LIVE 音频**可能**绕开该路径，或者未满足其处理条件；尚没有 LIVE 实测证实。

本插件在 TikTok 进程尝试被动监测：

| 计数器 | 含义 | 限制 |
|---|---|---|
| `AURender`, `bus1` | AudioUnitRender 调用次数与 bus 1 次数 | bus 1 不一定就是 LIVE 实际上传的音频 |
| `AUCreate` | AudioComponentInstanceNew 创建次数 | 无法看到已存在或其他进程创建的组件 |
| `RemoteIO` | RemoteIO 组件创建计数 | 只对当前进程可见 |
| `VoiceProcessingIO` | VoiceProcessingIO 组件创建计数 | LIVE 可能用音频单元但不走这里的 API |
| `AUSetProperty` | AudioUnitSetProperty 调用次数 | 计数不代表配置成功 |
| `setBus1FourByte` | element=1 且属性长度 4 字节的次数 | **只是线索，不是已确认输入开关** |
| `pcmNonempty` | 成功返回且第一个 Buffer 非空的次数 | 不会读取或保存 PCM 内容 |

**不会 Hook 原厂系统进程。不会存储任何声音。**

## 如何构建 DEB（推荐 GitHub Actions，不要求你在电脑安装 Xcode）

1. 创建**你自己的** GitHub 仓库（可选 Private）。把本 ZIP 解压后的**所有文件和目录**上传到仓库根目录。注意必须保留隐藏文件夹 `.github/workflows/build.yml`。
2. GitHub 仓库页面 → `Actions` → `Build VCam LIVE Audio Diagnostics` → `Run workflow`。
3. 等工作流成功后，打开该次运行底部的 `Artifacts`，下载 `VCam-LIVE-Audio-Diagnostics-v0.2-TEST-ONLY`，解压，找到 `.deb`。
4. 该工作流在 `macos-15` 使用 Theos、iOS 16.5 SDK 和 arm64 编译，只把模块放入 TikTok App 进程。这份工作流尚未在你的 GitHub 仓库运行验证；若构建失败，请保留完整日志发回，我再修正。

也可在安装了 Xcode、Theos 和 iOS SDK 的 Mac 上运行：

```bash
export THEOS="$HOME/theos"
make clean package FINALPACKAGE=1
```

目前分析环境没有可完整验证的 iOS Theos+Xcode 设备编译环境，因此 ZIP **不附带声称已可安装的 DEB**。

## 安装与测试（先选一台备用手机）

1. 备份已有 VCam 1.1.0 `.deb`，确保 Sileo 正常可用；不要卸载或者覆盖原版 VCam。此模块的包名为 `com.vcam.liveprobe`。
2. 在备用 iPhone 通过 Sileo/Filza 安装编译出来的 `.deb`，按越狱管理器要求重新加载 tweak 或重启对应 App。务必彻底退出 TikTok 后重新打开。
3. 在 Filza 进入 TikTok 的 **App 数据容器** → `tmp`，确认生成 `VCamLiveProbe.log`。日志第一部分应出现 `MSHookFunction=found`、`Render=hooked` 等标记；如果显示 MISSING/NOT_HOOKED，先解决注入失败。
4. 保持 OBS 原声存在、VCam 画面正常，在 **TikTok 普通录像** 停留 20–30 秒，记下大致时间。
5. 切换到符合平台规定的**真实实时直播测试**，停留 20–30 秒，记下开始和结束时间，并用另一设备从观众端验证麦克风来源。
6. 取出 `VCamLiveProbe.log` 并发回，同时说明普通录像和 LIVE 各自的时间段、设备型号和系统版本。日志只有计数和时间戳，不含音频。
7. 电脑有 Python 3 时可运行 `python analyze_log.py VCamLiveProbe.log`，比较每 5 秒的 `bus1`、`VoiceProcessingIO`、`RemoteIO` 变化。

注意：TikTok 在切换功能时可能会重启进程，导致计数归零。日志中的 `#` 初始化行可以用来识别这种情况。

## 测试结果如何继续开发

| 现象 | 下个动作 |
|---|---|
| LIVE `bus1` 明显增长，而 OBS 声音仍没出现 | 检查该音频缓冲是否属于直播上传链路、PCM 格式、音轨路由与音频会话；随后才考虑音频桥接 |
| 普通录像有 `bus1`，LIVE 没有 | 进一步追踪 LIVE 在哪一进程/框架采集麦克风，而不是凭空写 AudioUnit 注入补丁 |
| LIVE 的 VoiceProcessingIO 创建增加 | 指向语音处理 I/O，可重点核查输入 bus 和会话模式；仍不是完整修复证据 |
| 全部为零 | 先验证 ElleKit Hook 生效、TikTok Bundle ID 是否正确；不能据此断言 LIVE 不使用这些接口 |

**真正修复仍需要第二阶段：**找到 OBS 解码后的 PCM 缓冲如何安全跨进程共享，并通过直播使用的音频回调读取，同时处理时间戳、采样率、PCM 格式、重采样、耳返/回声和断流恢复。不能通过简单把 `.plist` 加上 TikTok 就实现。

## 卸载 / 回退

在 Sileo 卸载 `VCam LIVE Audio Diagnostics`（包名 `com.vcam.liveprobe`），重启 TikTok。原版 `VCam 1.1.0` 保留不受本模块覆盖。如果 App 崩溃，先在越狱安全模式下移除这个诊断包。发生异常时暂停测试，不要在重要直播中继续使用。

## 来源说明

- 你的 `com.vcam_1.1.0_iphoneos-arm64.deb`：用于验证原版包结构、`mediaserverd` 的注入过滤器与可识别音频 Hook（静态分析）。
- Apple 文档的 I/O Unit bus 定义和 Theos rootless 规范：用于判断应观察的音频路径与打包结构。
- **本诊断源码为独立新增模块，未声称来自原作者，未合并/篡改其二进制。**
