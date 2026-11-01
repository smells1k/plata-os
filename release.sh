#!/bin/sh
# Собрать прошивку и выложить на GitHub
# Запуск:  ./release.sh "что нового"
set -e
VER="$1"
SK=~/esp32-flasher/examples/board-os
CLI=~/.cache/ac/arduino-cli
export ARDUINO_DIRECTORIES_DATA=~/.cache/ard/data
export ARDUINO_DIRECTORIES_DOWNLOADS=~/.cache/ard/dl
export ARDUINO_DIRECTORIES_USER=~/.cache/ard/user

[ -n "$VER" ] && sed -i "s/OS_VER = \"[0-9.]*\"/OS_VER = \"$VER\"/" "$SK/board-os.ino"
"$CLI" compile -b esp32:esp32:esp32 --output-dir /tmp/fwR "$SK" > /tmp/build.log 2>&1 || {
  grep error /tmp/build.log | head -5; exit 1; }
grep "Sketch uses" /tmp/build.log

cp /tmp/fwR/board-os.ino.merged.bin firmware/ПЛАТА-ОС.bin
cp /tmp/fwR/board-os.ino.bin        firmware/ПЛАТА-ОС-для-WiFi.bin
cp "$SK"/{board-os.ino,os_extra.ino,os_games.ino,ui_core.h,ui_font.h,board_config.h,User_Setup.h} sketch/
cp ~/esp32-flasher/pc-app/ЭКРАН-ПЛАТЫ.py app/
rm -rf /tmp/fwR

git add -A
git commit -m "${VER:+сборка $VER} ${2:-обновление}"
git push
echo "готово: https://github.com/smells1k/plata-os"
