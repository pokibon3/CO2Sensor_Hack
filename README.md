# CO2モニター基板（DM72D）の解析と自作ファームウェア

AT32F415CBT7＋LT7680B＋4.3インチ液晶（480×272）のCO2モニター基板を解析し、その上で動くソフトウェアを作成した。

## 構成

| フォルダ | 内容 |
|---|---|
| `analysis/` | 基板の解析結果：レポート（`README.md`）、写真。工場ファームとW25Q32のダンプはリポジトリに含めない |
| `software/colorbar_sample/` | LT7680Bのカラーバー表示サンプル（最初の動作確認用） |
| `software/invaders/` | ALIEN RAID（8080エミュレータ上で動くインベーダー風ゲーム）とW25Q32読み書きツール |
| `tools/` | `backup_flash.py`：MCU内蔵FlashとW25Q32をSWDでバックアップする（使い方は `tools/README.md`）。ダンプは各自このツールで取得する。OpenOCDの環境構築は `tools/openocd_setup.md` |

## 要点

- MCU–LT7680B間はSPI2（PB12＝CS）。LT7680Bは水晶を持たず、PA8から8 MHzのクロックを受け取る
- PA7で電源をラッチしている。MCUがリセットされると電源が落ちる。工場ファームはWDTを使う
- 書き込みはSWD（J1）で行う。手順は `software/invaders/README.md` の「Flashing」を参照
- W25Q32（4 MB）はLT7680BのSPIマスター（nSS1）を経由してMCUから読み書きできる
