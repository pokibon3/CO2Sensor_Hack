# OpenOCDの環境構築

`tools/backup_flash.py`、`software/invaders/tools/sflash.py`、[flashing.md](flashing.md) の書き込み手順は、ArteryTek版OpenOCD（PlatformIOのパッケージ `tool-openocd-at32`）を使う。作者の環境はmacOS（Apple Silicon）、PlatformIO Core 6.1.19、`tool-openocd-at32` 0.1100.260710（`Open On-Chip Debugger 0.11.0+dev-ga8daba8`）。

## 標準のOpenOCDは使えない

Homebrewの `openocd`（0.12.0）にはAT32F415のターゲット設定（`target/at32f415xx.cfg`）とFlashドライバがない。必ずArteryTek版を使う。

## 1. PlatformIOを入れる

```sh
brew install platformio
pio --version
```

## 2. AT32用プラットフォームとOpenOCDを入れる

`software/invaders` を一度ビルドすると、`platformio.ini` に書かれたプラットフォーム（`https://github.com/ArteryTek/platform-arterytekat32.git`）と、ツールチェーン、OpenOCDがまとめてインストールされる。

```sh
cd software/invaders
pio run -e sflash
```

インストール先は `~/.platformio/packages/tool-openocd-at32`。ビルドせずにOpenOCDだけ入れる場合は次のとおり。

```sh
pio pkg install -g -t https://github.com/amoxu/tool-openocd-at32.git
```

## 3. インストールを確認する

リポジトリのルートで実行する。

```sh
python3 tools/openocd.py --version
ls ~/.platformio/packages/tool-openocd-at32/scripts/target/at32f415xx.cfg
```

OpenOCDの実行ファイルは `tool-openocd-at32/bin-<OS>_<CPU>/` にある。`tools/openocd.py` は実行中のOSとCPUに合うものを選び、`-s <scripts>` を付けて起動するラッパー。対応するのはmacOS、Linux（x86_64、aarch64、armv7l）、Windows。書き込みコマンド、`backup_flash.py`、`sflash.py` も同じ判定を使う。

## 4. デバッガを確認する

debugprobeファームを入れたRP2040-Zero（作り方は [debugger_connection.md](debugger_connection.md)）はCMSIS-DAPv2として認識される。macOSではドライバは不要。

```sh
system_profiler SPUSBDataType | grep -i -B2 -A6 cmsis-dap
```

Vendor ID `0x2e8a`、Product ID `0x000c` が見えればよい。

## 5. 基板との接続を試す

J1の配線、電源の入れ方、接続確認のコマンドとエラーの対処は [debugger_connection.md](debugger_connection.md) を参照。
