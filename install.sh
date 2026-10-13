#!/bin/bash

# 確保指令以 root 權限執行
if [ "$EUID" -ne 0 ]; then
  echo "[-] 請使用 sudo 執行此安裝腳本：sudo ./install.sh"
  exit 1
fi

# 取得目前專案所在的絕對路徑（自動對應當前使用者與資料夾）
PROJECT_PATH=$(pwd)
CURRENT_USER=$(logname 2>/dev/null || echo $SUDO_USER)

echo "[+] 開始安裝 BLE UART 服務..."
echo "[+] 專案路徑: $PROJECT_PATH"
echo "[+] 執行身分: $CURRENT_USER"

# 1. 編譯專案
echo "[+] 正在編譯 ble_server..."
make clean
make
if [ $? -ne 0 ]; then
  echo "[-] 編譯失敗，請檢查 C 程式碼或 Makefile。"
  exit 1
fi

# 2. 動態產生 systemd 服務檔
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
WorkingDirectory=$PROJECT_PATH
ExecStart=$PROJECT_PATH/ble_server
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# 3. 註冊並啟動服務
echo "[+] 啟用並啟動 Systemd 服務..."
systemctl daemon-reload
systemctl enable bleserver.service
systemctl restart bleserver.service

echo "=========================================="
echo "[+] 安裝完成！ble_server 已成功設定為背景常駐。"
echo "[+] 您可以隨時輸入以下指令檢查狀態："
echo "    sudo systemctl status bleserver.service"
echo "=========================================="
