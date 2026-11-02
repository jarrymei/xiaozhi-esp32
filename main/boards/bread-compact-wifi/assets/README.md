# 唱歌麦克风素材

`singing_microphone.png` 为本项目聊天中生成、经用户确认方向的卡通麦克风与小手透明素材。用户要求：尺寸较小，手背朝向用户，向内握住麦克风。素材仅用于屏幕，不代表新增实体手臂。

`singing_microphone.h` 是84×84的LVGL ARGB8888描述和28224字节BGRA直通Alpha像素，常量保存于Flash；固件不依赖PNG解码、网络下载或本机绝对路径。

在Windows PowerShell中重新生成：

```powershell
./main/boards/bread-compact-wifi/assets/generate_singing_microphone.ps1
```

脚本使用系统 `System.Drawing` 对PNG作缩放和格式转换，不重新设计图像。修改源图后重新生成头文件并编译。保留PNG、脚本和头文件以便复现；无需修改公共构建配置。

动画使用整个素材轻摆，保证手和麦克风的握持关系；被否决的高低眼、连续wink、歪脸和闪光不作为当前实现。具体触发、收尾和验证说明见[双眼表情实验](../../../../docs/experiments/04-robot-eyes/README.md)。

当前布局中源素材仍为84×84，由绘制描述按219/256缩放至约72×72，并重新定位给底部字幕留空间；不需要重新生成像素头文件。当前整套屏幕效果已由用户实屏确认可用，字幕以已恢复的原LCD实现为准。
