# OpenOCDの環境構築

`backup_flash.py`、`software/invaders/tools/sflash.py`、各READMEの書込み手順は、ArteryTek版OpenOCD（PlatformIOのパッケージ `tool-openocd-at32`）を使う。作者の環境はmacOS（Apple Silicon）、PlatformIO Core 6.1.19、`tool-openocd-at32` 0.1100.260710（`Open On-Chip Debugger 0.11.0+dev-ga8daba8`）。

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

OpenOCDの実行ファイルは `tool-openocd-at32/bin-<OS>_<CPU>/` にある。`tools/openocd.py` は実行中のOSとCPUに合うものを選び、`-s <scripts>` を付けて起動するラッパー。対応するのはmacOS、Linux（x86_64、aarch64、armv7l）、Windows。各READMEのコマンド、`backup_flash.py`、`sflash.py` も同じ判定を使う。

## 4. Debug Probeを確認する

Raspberry Pi Debug Probe（またはdebugprobeファームを入れたPico）はCMSIS-DAPv2として認識される。macOSではドライバは不要。

```sh
system_profiler SPUSBDataType | grep -i -B2 -A6 cmsis-dap
```

Vendor ID `0x2e8a`、Product ID `0x000c` が見えればよい。J1との配線は `../analysis/README.md` を参照。

## 5. 基板との接続を試す

S2で基板の電源を入れてから、リポジトリのルートで実行する。Probeからは給電しない。

```sh
python3 tools/openocd.py \
  -f interface/cmsis-dap.cfg -f target/at32f415xx.cfg -c "adapter speed 1000" \
  -c "init; targets; exit"
```

`SWD DPIDR 0x2ba01477` が表示されれば接続できている。`Error connecting DP: cannot read IDR` が出る場合は、配線を短くし、GNDを確実にしてから再試行する。`reset` を含むコマンドは電源ラッチを外して基板の電源が落ちるため、接続確認では使わない。
