# 南荒白板 · Nanhara Board

南荒工具坊的本機白板，面向 Windows 觸摸大屏。原生 Windows 預覽版支援多頁、畫筆、
橡皮擦、逐頁撤銷、清空與全螢幕。草稿只保留到本次執行結束。

## Windows 預覽版

從 [Releases](https://github.com/nanhara/nanhara-board-oss/releases) 下載 x64 portable
ZIP 和對應 SHA-256，校驗後解壓再執行。預覽包供測試，尚無正式簽名安裝器。
第一次使用先做觸控、翻頁和離線測試。環境與詳情見
[測試指南](native/windows/Nanhara.Board.InkDesktopHost/TEST_GUIDE.md)。

開啟白板時會在背景讀取本倉庫的預覽版號。發現較新版時，頂欄出現「查看新版」。
點按可開啟 GitHub Releases，由使用者選擇何時下載、關閉白板和更換程式。
此版號請求不傳白板內容或檔案。離線與版號格式異常時可照常書寫。

## 從原始碼構建

在 Windows x64 用 Visual Studio 2022 Build Tools 和 Windows 10/11 SDK。

```powershell
msbuild native/windows/Nanhara.Board.InkDesktopHost/Nanhara.Board.InkDesktopHost.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

本倉庫也包含早期 Tauri 客戶端和 Web UI。使用支援的現代 Node.js 版本，執行
`npm ci`、`npm run check`、`npm test`，桌面開發用 `npm run tauri -- dev`。
原生 Windows 預覽版與 Tauri 客戶端使用各自的文件和更新流程。

## 授權

[MIT](LICENSE)。第三方相依套件沿用各自的授權。
