# 書き込み

自作ファームをSWDで内蔵Flashに書き込む手順。デバッガの準備と配線、接続確認は [debugger_connection.md](debugger_connection.md) を参照。工場ファームへ戻すときは [flash_backup.md](flash_backup.md)。

## 電源とリセット

- 基板の電源は**PWRボタンを1秒押して**入れる。デバッガから給電しない
- 電源はPA7のラッチで保持している。MCUがリセットされるとラッチが外れて電源が落ちる。このため `pio run -t upload`（OpenOCDの `program`）は使えない。haltしてから書き込み、最後に `reset run` する
- PWRを押している間はボタン自体が給電するので、リセットしても電源は落ちない
- 自作ファームの動作中（LT7680Bと液晶が動いている間）は、DPは見えるがメモリアクセスが失敗する。**PWRを押したまま**接続・書き込みする。DOOMは、halt後は電源OFFの長押しを数えない
- Flashが空や壊れている（電源を保持するファームがない）ときは、書き込みの間ずっとPWRを押し続ける
- 工場ファームはWDTを使う。haltしたままだとWDTでリセットされて電源が落ちるため、halt直後に `mww 0xE0042004 0x300`（`DEBUG_CTRL` のWDT/WWDT停止）を書く

## 書き込みコマンド

各プロジェクトのディレクトリ（`software/<name>/`）で `pio run` してから実行する。

```sh
python3 ../../tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; flash write_image erase .pio/build/at32f415cbt7/firmware.elf; verify_image .pio/build/at32f415cbt7/firmware.elf; cortex_m maskisr auto; reset run; exit"
```

| 書き込むもの | ELF |
|---|---|
| `software/colorbar_sample`、`software/invaders`、`software/doomlike` | `.pio/build/at32f415cbt7/firmware.elf` |
| W25Q32読み書き用ファーム（`software/invaders` の `pio run -e sflash`） | `.pio/build/sflash/firmware.elf` |

コマンドの内容：

| 操作 | 理由 |
|---|---|
| `halt` | リセットすると電源が落ちるため、止めた状態で書き込む |
| `cortex_m maskisr on` | ステップ中の割り込みを止める |
| `mww 0xE0042004 0x300` | WDT/WWDTをhalt中に止める |
| `mww 0xE000E010 0` | SysTickを止める |
| `mww 0xE000E180 0xFFFFFFFF`、`mww 0xE000E280 0xFFFFFFFF` | NVICの割り込みを全部無効にし、保留を消す |
| `reset run` | 新しいファームで起動する（PA7のラッチは新しいファームが掛け直す） |

割り込み（SysTick、サウンド用の48 kHz TMR3など）を止めないと、書き込み中の割り込みが消去済みのベクタに飛び、書き込みがタイムアウトする。

`verify_image` が `checksum mismatch - attempting binary compare` と出ることがある。続くバイト比較で差分が出なければ問題ない。
