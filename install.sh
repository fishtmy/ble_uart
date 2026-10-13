#!/bin/bash

# 確保指令以 root 權限執行
if [ "$EUID" -ne 0 ]; then
  echo "[-] 請使用 sudo 執行此安裝腳本：sudo ./install.sh"
  exit 1
fi

echo "[+] 開始安裝 BLE UART 服務..."

# 1. 編譯專案
echo "[+] 正在編譯 ble_server..."
make clean
make
if [ $? -ne 0 ]; then
  echo "[-] 編譯失敗，請檢查 C 程式碼或 Makefile。"
  exit 1
fi

# 2. 將編譯好的執行檔複製到系統全域固定路徑（例如 /usr/local/bin/）
INSTALL_PATH="/usr/local/bin/ble_server"
echo "[+] 正在將執行檔安裝至系統目錄: $INSTALL_PATH"
cp ble_server $INSTALL_PATH
chmod +x $INSTALL_PATH

# 3. 動態產生 systemd 服務檔（固定指向系統路徑，不再依賴下載資料夾）
SERVICE_FILE="/etc/systemd/system/bleserver.service"
echo "[+] 正在建立系統服務檔: $SERVICE_FILE"

cat << EOF > $SERVICE_FILE
[Unit]
Description=BLE UART Server for Raspberry Pi
After=network.target bluetooth.service
Requires=bluetooth.service

[Service]
Type=simple
User=root
ExecStart=$INSTALL_PATH
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# 4. 註冊並啟動服務
echo "[+] 啟用並啟動 Systemd 服務..."
systemctl daemon-reload
systemctl enable bleserver.service
systemctl restart bleserver.service

echo "=========================================="
echo "[+] 安裝完成！ble_server 已成功安裝並常駐。"
echo "[+] 現在您可以安全地將 /home/pi/ble_uart 資料夾刪除！"
echo "[+] 檢查狀態指令：sudo systemctl status bleserver.service"
echo "=========================================="
