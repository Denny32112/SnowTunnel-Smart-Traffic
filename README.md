# 🚗 國道五號 (雪山隧道) 智慧車流決策系統


這是一個基於 C++ 開發的工具，透過串接交通部 TDX API，即時抓取雪山隧道的車速，並分析該走「內側」還是「外側」車道比較快。

## 🚀 功能
* 自動抓取 TDX 即時車速資料 (OAuth 2.0 認證)
* 自動過濾出國道五號 (雪隧) 北上與南下路段
* 智慧判斷內外車道速差，提供駕駛建議

## 🛠️ 使用技術
* C++
* cURL (網路請求)
* nlohmann/json (JSON 解析)

## 📦 如何執行(需創建TDX帳戶取得CLIENT_ID 和 CLIENT_SECRET)
1. 確保電腦有 `g++` 和 `curl`。
2. 在程式碼中填入你的 TDX API ID 與 Secret。
3. 編譯並執行：
   ```bash
   g++ main.cpp -o main.exe
   .\main.exe