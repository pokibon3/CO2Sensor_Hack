# tools

## backup_flash.py — 工場ファームとFlashのバックアップ

AT32F415の内蔵FlashとW25Q32の内容をSWDで読み出し、ファイルに保存する。

### 準備

- Raspberry Pi Debug ProbeをJ1（SWCLK、SWDIO、GND）に接続する。配線は `../analysis/README.md` を参照
- 基板はS2で電源を入れる。Probeからは給電しない
- PlatformIOとAT32用OpenOCD（`~/.platformio/packages/tool-openocd-at32`）が必要。環境構築は `openocd_setup.md` を参照。sflashファームが未ビルドなら、`all` の実行時に `pio run -e sflash` でビルドする

### 使い方

```sh
python3 tools/backup_flash.py mcu        # 内蔵Flash 128 KB とユーザーシステムデータ 48 B
python3 tools/backup_flash.py spiflash   # W25Q32 4 MB（sflashファームが動いていること）
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

保存時に、作者の個体から取得したダンプのSHA256（`analysis/README.md` に記載）と比較し、一致するかどうかを表示する。ダンプそのものはメーカーの著作物のため、リポジトリには含めない。`backups/` もコミットしないこと。

### `all` の流れ

1. MCUのWDTを止めてhaltし、内蔵Flashとユーザーシステムデータを2回読む。2回の内容が一致しなければ中止する
2. sflashファーム（`software/invaders` の `env:sflash`）をMCUに書き込む。この時点で内蔵Flashは上書きされる
3. sflashファーム経由でW25Q32を読む。JEDEC IDが `EF4016` でなければ中止する
4. 手順1のバックアップをMCUに書き戻し、検証する

手順2と4は書込み後にMCUをリセットする。リセットすると電源ラッチが外れるため、「Hold S2 and press Enter」と表示されたらS2を押したままEnterを押す。

### 注意

- 先に `mcu` だけを実行し、2回の読出しが一致して保存できたことを確認してから `all` を使う
- 内蔵Flashが全部0と読める場合は、読出し保護（FAP）が有効の可能性があるとして中止する。FAPの解除は全消去を伴うため、このスクリプトでは解除しない
- 通信が不安定な場合は配線を短くし、GNDを確実にする
