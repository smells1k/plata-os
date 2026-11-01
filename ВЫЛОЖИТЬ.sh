#!/bin/sh
# Выложить текущее состояние на GitHub одной командой
cd ~/plata-os || exit 1
git add -A
git commit -m "${1:-ПЛАТА-ОС: обновление}" || exit 0
git push
