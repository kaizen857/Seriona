#!/bin/sh
# Seriona AppImage 启动器（linuxdeploy --custom-apprun 使用，2026-09-05 引入）。
#
# 背景：Qt 仅在 Flatpak/Snap 会话自动选择 xdgdesktopportal 平台主题；普通宿主会话中
# AppImage 必须显式设置 QT_QPA_PLATFORMTHEME，QFileDialog / Qt.labs.platform 的文件夹
# 对话框才会经 xdg-desktop-portal 由桌面环境渲染（KDE/GNOME 原生对话框）。
# 用户若已自行设置（如 qt6ct 等主题工具），此处不覆盖。
#
# type2 runtime 启动时提供 $APPDIR（AppImage 挂载点）；CI 用 --appimage-extract 解包后
# 直接执行本脚本时无此变量，回退为脚本所在目录（解包根 = AppDir 根）。
if [ -z "${APPDIR:-}" ]; then
    APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi
if [ -z "${QT_QPA_PLATFORMTHEME:-}" ]; then
    export QT_QPA_PLATFORMTHEME=xdgdesktopportal
fi
exec "$APPDIR/usr/bin/seriona" "$@"
