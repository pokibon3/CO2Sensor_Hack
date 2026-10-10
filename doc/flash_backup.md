# 工場ファームとFlashのバックアップ（`tools/backup_flash.py`）

AT32F415の内蔵FlashとW25Q32の内容をSWDで読み出し、ファイルに保存する。

### 準備

- デバッガ（RP2040-Zero）をJ1につなぎ、PWRで基板の電源を入れる。配線は [debugger_connection.md](debugger_connection.md)、電源の注意は [flashing.md](flashing.md)
- PlatformIOとAT32用OpenOCD（`~/.platformio/packages/tool-openocd-at32`）が必要。環境構築は [openocd_setup.md](openocd_setup.md) を参照。sflashファームが未ビルドなら、`all` の実行時に `pio run -e sflash` でビルドする

### 使い方

```sh
python3 tools/backup_flash.py mcu        # 内蔵Flash 128 KB とユーザーシステムデータ 48 B
python3 tools/backup_flash.py spiflash   # W25Q32 4 MB（sflashファームが動いていること。読出し中はPWRを押し続ける）
python3 tools/backup_flash.py all        # 両方。最後にMCUの内容を書き戻す
```

| オプション | 内容 |
|---|---|
| `-o DIR` | 保存先。デフォルトは `backups/YYYYmmdd-HHMMSS/`。既にバックアップがあるディレクトリには保存しない |
| `--no-restore` | `all` で、MCUを書き戻さずsflashファームのままにする |

### 出力

| ファイル | 内容 |
|---|---|
| `factory_firmware.bin` | 内蔵Flash全体（0x08000000〜、128 KB） |
| `user_system_data.bin` | ユーザーシステムデータ（0x1FFFF800〜、48 B） |
| `w25q32_factory.bin` | W25Q32全体（4 MB） |
| `SHA256SUMS` | 上記のSHA256 |

保存時に、作者の個体から取得したダンプのSHA256（[board_analysis.md](board_analysis.md) に記載）と比較し、一致するかどうかを表示する。ダンプそのものはメーカーの著作物のため、リポジトリには含めない。`backups/` もコミットしないこと。

### `all` の流れ

1. MCUのWDTを止めてhaltし、内蔵Flashとユーザーシステムデータを2回読む。2回の内容が一致しなければ中止する
2. sflashファーム（`software/invaders` の `env:sflash`）をMCUに書き込む。この時点で内蔵Flashは上書きされる
3. sflashファーム経由でW25Q32を読む。JEDEC IDが `EF4016` でなければ中止する
4. 手順1のバックアップをMCUに書き戻し、検証する

手順2と4は書込み後にMCUをリセットする。リセットすると電源ラッチが外れるため、「Hold PWR and press Enter」と表示されたらPWRを押したままEnterを押す。

**手順2から最後まで、PWRを押し続ける。** W25Q32の読出しは約10 KB/sで、4 MBに約7分かかる。「Saved to ... PWR can be released.」と表示されたら離してよい。指を替えるなど数秒離れる程度なら、失敗したブロックを読み直して続きから読む。10秒以上離れると中止し、実行し直すと最初から読み直しになる。

### 注意

- 先に `mcu` だけを実行し、2回の読出しが一致して保存できたことを確認してから `all` を使う
- 内蔵Flashが全部0と読める場合は、読出し保護（FAP）が有効の可能性があるとして中止する。FAPの解除は全消去を伴うため、このスクリプトでは解除しない
- 通信が不安定な場合は [debugger_connection.md](debugger_connection.md) の「接続確認」を参照
- sflashファームの動作中（LT7680Bと液晶が動いている間）は、電源ラッチ（PA7）だけではSWDのメモリアクセスが失敗する。PWRを押していれば読める。電源の余裕が足りないためと考えられるが、電圧は実測していない（2026-10-09確認）
- W25Q32は各ブロックを2回読み、一致したものだけを採用する。OpenOCDが途中で終了した場合は起動し直す
- 書き戻し時の `verify_image` の `checksum mismatch` は問題ない（[flashing.md](flashing.md)。2026-10-09、書き戻し後の読み直しでSHA256一致を確認）
