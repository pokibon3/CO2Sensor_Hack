# 純正ファームとFlashのバックアップ（`tools/backup_flash.py`）

AT32F415の内蔵FlashとW25Q32の内容をSWD経由で読み出し、ファイルに保存する。

### 準備

- デバッガ（RP2040-Zero）をJ1につなぎ、PWRで基板の電源を入れる。配線は [debugger_connection.md](debugger_connection.md)、電源の注意点は [flashing.md](flashing.md) を参照
- PlatformIOとAT32用OpenOCD（`~/.platformio/packages/tool-openocd-at32`）が必要。環境構築は [openocd_setup.md](openocd_setup.md) を参照。sflashファームがまだビルドされていなければ、`all` の実行時に `pio run -e sflash` で自動的にビルドされる

### 使い方

```sh
python3 tools/backup_flash.py mcu        # 内蔵Flash 128 KBとユーザーシステムデータ48 B
python3 tools/backup_flash.py spiflash   # W25Q32 4 MB（sflashファームが動作していること。読み出し中はPWRを押し続ける）
python3 tools/backup_flash.py all        # 上の両方。最後にMCUの内容を書き戻す
```

| オプション | 内容 |
|---|---|
| `-o DIR` | 保存先。デフォルトは `backups/YYYYmmdd-HHMMSS/`。すでにバックアップが入っているディレクトリには保存しない |
| `--no-restore` | `all` の最後にMCUを書き戻さず、sflashファームのままにする |

### 出力ファイル

| ファイル | 内容 |
|---|---|
| `factory_firmware.bin` | 内蔵Flash全体（0x08000000から128 KB） |
| `user_system_data.bin` | ユーザーシステムデータ（0x1FFFF800から48 B） |
| `w25q32_factory.bin` | W25Q32全体（4 MB） |
| `SHA256SUMS` | 上記ファイルのSHA256 |

保存時には、作者の個体から取得したダンプのSHA256（[board_analysis.md](board_analysis.md) に記載）と比較し、一致したかどうかを表示する。ダンプ自体はメーカーの著作物なのでリポジトリには含めていない。`backups/` もコミットしないこと。

### `all` の処理の流れ

1. MCUのWDTを止めてhaltし、内蔵Flashとユーザーシステムデータを2回読む。2回の内容が一致しなければ中止する
2. sflashファーム（`software/invaders` の `env:sflash`）をMCUに書き込む。ここで内蔵Flashは上書きされる
3. sflashファーム経由でW25Q32を読む。JEDEC IDが `EF4016` でなければ中止する
4. 手順1で取ったバックアップをMCUに書き戻し、検証する

手順2と4では、書き込み後にMCUをリセットする。リセットすると電源ラッチが外れるので、「Hold PWR and press Enter」と表示されたら、PWRを押したままEnterを押す。

**手順2から最後まで、PWRを押し続けること。** W25Q32の読み出し速度は約10 KB/sで、4 MB読むのに約7分かかる。「Saved to ... PWR can be released.」と表示されたら離してよい。押す指を替えるなどで数秒離れる程度なら、失敗したブロックを読み直して続きから再開する。10秒以上離すと中止され、実行し直すと最初から読み直しになる。

### 注意

- まず `mcu` だけを実行し、2回の読み出し結果が一致して保存できたことを確認してから `all` を使う
- 内蔵Flashがすべて0と読める場合は、読み出し保護（FAP）が有効になっている可能性があるので中止する。FAPを解除すると全消去されるため、このスクリプトでは解除しない
- 通信が不安定な場合は [debugger_connection.md](debugger_connection.md) の「接続確認」を参照
- sflashファームの動作中（LT7680Bと液晶が動いている間）は、電源ラッチ（PA7）だけではSWDのメモリアクセスに失敗する。PWRを押していれば読める。電源の余裕が足りないためと考えているが、電圧は実測していない（2026-10-09に確認）
- W25Q32は各ブロックを2回読み、内容が一致したものだけを採用する。途中でOpenOCDが終了した場合は自動的に起動し直す
- 書き戻し時に `verify_image` が `checksum mismatch` と表示しても問題ない（[flashing.md](flashing.md) を参照。2026-10-09、書き戻し後に読み直してSHA256の一致を確認済み）
