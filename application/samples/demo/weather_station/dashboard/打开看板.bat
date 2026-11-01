@echo off
chcp 65001 >nul
title 桌面智能气象站 - 上位机看板
cd /d "%~dp0"

echo ============================================
echo   桌面智能气象站 - 上位机看板
echo ============================================
echo.
echo 正在用浏览器打开...
echo.
echo 注意：请务必用浏览器直接打开本页面，
echo       不要用 IDE 的预览窗口（预览会拦截 WebSocket）。
echo.

rem 优先用 Edge（Windows 自带），其次默认浏览器
set "EDGE86=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
set "EDGE64=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"

if exist "%EDGE86%" (
    start "" "%EDGE86%" "%~dp0index.html"
    goto opened
)
if exist "%EDGE64%" (
    start "" "%EDGE64%" "%~dp0index.html"
    goto opened
)
start "" "%~dp0index.html"

:opened
echo 已启动浏览器。
echo.
echo 如果页面提示“连不上 Broker”，请依次检查：
echo   1) 电脑上 EMQX 是否已启动（命令行执行 emqx start）
echo   2) 8083 端口是否在监听（netstat -an ^| findstr 8083）
echo   3) 看板顶部的 Broker 地址是否为这台电脑当前的 IP
echo.
timeout /t 5 >nul
exit
