# 書き込み

自作ファームをSWD経由で内蔵Flashに書き込む手順。デバッガの準備、配線、接続確認は [debugger_connection.md](debugger_connection.md) を参照。純正ファームに戻す方法は [flash_backup.md](flash_backup.md) にある。

## 電源とリセットの注意点

- 基板の電源は**PWRボタンを1秒押して**入れる。デバッガからは給電しない
- 電源はPA7のラッチで保持されていて、MCUがリセットされるとラッチが外れて電源が落ちる。このため `pio run -t upload`（OpenOCDの `program`）は使えない。haltしてから書き込み、最後に `reset run` で起動する
- PWRを押している間はボタン自体が通電させるので、リセットしても電源は落ちない
- 自作ファームの動作中（LT7680Bと液晶が動いている間）は、DPは見えてもメモリアクセスに失敗する。接続と書き込みは**PWRを押したまま**行う。なお、DOOMはhaltされると電源OFFの長押し時間を数えなくなる
- Flashが空、または壊れている（電源を保持するファームがない）ときは、書き込みが終わるまでPWRを押し続ける
- 純正ファームはWDTを使っている。haltしたままだとWDTでリセットされて電源が落ちるため、halt直後に `mww 0xE0042004 0x300`（`DEBUG_CTRL` でWDTとWWDTを停止）を実行する

## DOOMゲームの書き込み（`flash_doom`）

DOOMゲーム（`software/doomlike`）は、次のコマンドでビルドから書き込みまでを行える。どのディレクトリから実行してもよい。中で実行するのは、次の「書き込みコマンド」の `halt` を `reset halt` に置き換えた手順。

```sh
./tools/mac/flash_doom.command             # pio runでビルドしてから書き込む
./tools/mac/flash_doom.command --no-build  # ビルド済みのfirmware.elfを書き込む
```

Windowsでは `tools\win\flash_doom.bat`、Finderからは `tools/mac/flash_doom.command` をダブルクリックする。`-y` を付けると、Enterを待たずに書き込む。

「Hold PWR and press Enter」と表示されたら、PWRを押したままEnterを押し、「Done. PWR can be released.」と表示されるまで押し続ける。DOOMはリセット後、PWRが1秒押され続けて初めて電源ラッチをかけるので、書き込み後も約2秒押し続けるよう表示が出る。そのあと `g_diag` をSWDで読み、起動できたか（`stage 0x3 (running)`）を表示する。起動していなければ「DOOM did not start」と `g_diag` の値を表示する。

DOOMの動作中にhaltして書き込むと、消去の直後に `timeout waiting for algorithm` で書き込みに失敗する。リセット直後のMCUになら書き込める（2026-11-19に確認。動作中のクロックや周辺機能のどれが原因かは未特定）。このため `flash_doom.py` は、PWRを押したまま `reset halt` してから書き込む（PWRを押していればリセットしても電源は落ちない）。それでも失敗した場合は、最大3回までやり直す。

## 書き込みコマンド

各プロジェクトのディレクトリ（`software/<name>/`）で `pio run` を実行してビルドしたあと、次のコマンドを実行する。

```sh
python3 ../../tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; halt; cortex_m maskisr on; mww 0xE0042004 0x300; mww 0xE000E010 0; mww 0xE000E180 0xFFFFFFFF; mww 0xE000E280 0xFFFFFFFF; flash write_image erase .pio/build/at32f415cbt7/firmware.elf; verify_image .pio/build/at32f415cbt7/firmware.elf; cortex_m maskisr auto; reset run; exit"
```

DOOMなどの自作ファームが動いている上に書き込むときは、`halt` を `reset halt` に置き換え、PWRを押したまま実行する（上の「DOOMゲームの書き込み」を参照）。

| 書き込むもの | ELFファイル |
|---|---|
| `software/colorbar_sample`、`software/invaders`、`software/doomlike` | `.pio/build/at32f415cbt7/firmware.elf` |
| W25Q32読み書き用ファーム（`software/invaders` で `pio run -e sflash`） | `.pio/build/sflash/firmware.elf` |

各操作の意味：

| 操作 | 目的 |
|---|---|
| `halt` | リセットすると電源が落ちるので、CPUを止めた状態で書き込む。自作ファームの動作中は、PWRを押したまま `reset halt` にする |
| `cortex_m maskisr on` | ステップ実行中の割り込みを禁止する |
| `mww 0xE0042004 0x300` | halt中はWDTとWWDTを止める |
| `mww 0xE000E010 0` | SysTickを止める |
| `mww 0xE000E180 0xFFFFFFFF`、`mww 0xE000E280 0xFFFFFFFF` | NVICの割り込みをすべて無効にし、保留中の割り込みをクリアする |
| `reset run` | 新しいファームで起動する（PA7のラッチは新しいファームがかけ直す） |

割り込み（SysTickやサウンド用の48 kHz TMR3など）を止めておかないと、書き込み中に発生した割り込みが消去済みのベクタテーブルへ飛び、書き込みがタイムアウトする。

`verify_image` で `checksum mismatch - attempting binary compare` と表示されることがあるが、続くバイト単位の比較で差分が出なければ問題ない。
