#!/bin/bash
# press l3 on the console, fetch the newest evo_shot over FTP, save as PNG $1
cd "$(dirname "$0")/.."
docker compose run --rm ps5-dev ./tools/evo-remote.sh key l3 >/dev/null 2>&1
sleep 4
n=$(curl -s --max-time 15 ftp://192.168.0.12:2121/mnt/usb0/ | grep "evo_shot_[0-9]*.bmp" | awk '{print $6,$7,$8,$9}' | sort -k1,1M -k2,2n -k3,3 | tail -1 | awk '{print $4}')
echo newest=$n
curl -s --max-time 60 -o output/screenshots/tmp.bmp ftp://192.168.0.12:2121/mnt/usb0/$n
python -c "
from PIL import Image
im=Image.open('output/screenshots/tmp.bmp'); im.convert('RGB').resize((1920,1080)).save('output/screenshots/$1.png')"
