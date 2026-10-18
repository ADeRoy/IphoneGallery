# IphoneGallery

基于 Qt 6 + ElaWidgetTools 的本地 iPhone 图片、实况照片与视频浏览工具。

## 功能

- 打开或拖入 iPhone 导出目录，递归扫描 `HEIC / JPEG / PNG / MOV / MP4` 等文件。
- 从照片 EXIF 读取拍摄时间，按年/月分组并按拍摄时间排序；无 EXIF 时回退到文件夹月份或文件时间。
- 实况照片识别规则：同名图片 + `.MOV` 且视频时长不超过 3 秒；超过 3 秒的 MOV 按普通视频处理。
- 图库、实况照片、视频三个分类页面；支持搜索、排序和缩略图大小调整。
- 查看器默认小窗显示，支持圆角、窗口置顶、正常最大化、拖动和缩放。
- 查看器提供播放进度、暂停/播放、上一张/下一张、静音、缩放和信息面板。
- 实况照片打开后自动播放一次，结束后显示主图；按住画面循环播放，松开恢复主图。
- 自动处理 iPhone MOV 的旋转/镜像矩阵，并修正部分镜像视频的倒置问题。
- 缩略图右键菜单：打开文件所在目录、复制完整路径、在查看器中打开、删除到回收站。
- 默认浅色主题，可通过右上角按钮切换深色主题。

## 依赖

- Qt 6.11.1 MinGW 64-bit（或兼容的 Qt 6 版本）
- [ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools)
- FFmpeg（HEIC 解码、视频缩略图和视频元数据）
- CMake 3.20+、Ninja、MinGW 13.1

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DQT_SDK_DIR=C:/Qt/6.11.1/mingw_64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe

cmake --build build -j 12
```

若 ElaWidgetTools 不在 `D:/github/ElaWidgetTools`，请通过
`-DELAWIDGETTOOLS_DIR=<ElaWidgetTools 路径>` 指定。

构建时会自动复制 `ElaWidgetTools.dll`、Qt 运行库，以及可找到的 `ffmpeg.exe`。

## 运行

```powershell
D:\work\iphone\build\IphoneGallery.exe
```

也可以直接传入照片目录：

```powershell
D:\work\iphone\build\IphoneGallery.exe "D:\iphone图片视频-20260614"
```

## 说明

- 缩略图和查看器缓存位于 `%LOCALAPPDATA%\IphoneGallery\IphoneGallery\cache`。
- 右键“删除到回收站”会删除实际文件，可从系统回收站恢复；实况照片会同时删除图片和同名 MOV。
- 实况照片判定阈值位于 `core/PhotoLibrary.cpp` 的 `LivePhotoMaxDurationMs`。
- 查看器使用 `QVideoSink` 自绘视频帧，以正确处理旋转和镜像元数据。

## 目录结构

```
core/   照片扫描、EXIF、实况识别、缩略图与媒体工具
ui/     主窗口、图库页面、照片网格、查看器与视频画布
main.cpp
CMakeLists.txt
```

## 第三方库

- ElaWidgetTools（MIT License）
