# 環境構築

書き込みツールを使うためにPCに入れるものを、macOSとWindowsに分けてまとめる。デバッガ（RP2040-Zero）の作り方と基板との配線は [debugger_connection.md](debugger_connection.md)、書き込み方は [flashing.md](flashing.md) を参照。

作者の環境はmacOS（Apple Silicon）。Windowsの手順は、ツールとデバッガのファームの作りから考えたもので、作者はWindowsの実機で確かめていない。

## 何が必要か

| やりたいこと | 必要なもの |
|---|---|
| Web版で書き込む（DOOM、ALIEN RAID、純正ファームへの復元） | Python 3、ChromeかEdge |
| ゲームのソースを変えてビルドする | 上に加えて、PlatformIOとGit |
| コマンドライン版を使う（`flash_doom`、`backup_flash`、W25Q32の読み書きなど） | 上に加えて、ArteryTek版OpenOCD（PlatformIOが自動でインストールする） |

Web版で書き込むだけなら、PlatformIOもOpenOCDも要らない。ビルド済みのファーム（`tools/web/firmware/`）がリポジトリに入っている。Pythonは、ページをlocalhostで配信するためだけに使う。

## 1. リポジトリを取得する

```sh
git clone https://github.com/pokibon3/CO2Sensor_Hack.git
```

Gitがなければ、GitHubのページの「Code」→「Download ZIP」で取得して展開してもよい（ただし、ビルドするにはGitが必要。手順5を参照）。

## 2. Python 3を入れる

**macOS**：ターミナルで `python3 --version` を実行する。入っていなければ、表示されるダイアログでコマンドライン・デベロッパツールをインストールするか、`brew install python` を実行する。

**Windows**：[python.org](https://www.python.org/downloads/) のインストーラか、`winget install Python.Python.3.12` で入れる。インストーラでは「Add python.exe to PATH」にチェックを入れる。`tools\win\*.bat` は、Pythonランチャー（`py -3`）があればそれを、なければ `python` を使う。Microsoft Storeを開くだけの `python`（アプリ実行エイリアス）しかない状態では動かない。

確認：

```sh
python3 --version      # macOS
py -3 --version        # Windows
```

## 3. ブラウザ

Web版はWebUSBを使うので、ChromeかEdgeが必要。SafariとFirefoxはWebUSBに対応していない。

## 4. デバッガのドライバ

debugprobeファームを書き込んだRP2040-Zeroは、CMSIS-DAP v2（VID:PID `2E8A:000C`、名前 `Debugprobe on Pico (CMSIS-DAP)`）として認識される。

**macOS**：ドライバは不要。確認するには次を実行し、Vendor ID `0x2e8a`、Product ID `0x000c` が表示されればよい。

```sh
system_profiler SPUSBDataType | grep -i -B2 -A6 cmsis-dap
```

**Windows 10/11**：ドライバは不要で、**Zadigも使わない**。`tools/debugprobe_on_pico.uf2` はMS OS 2.0記述子（`WINUSB`）を持っているので、挿すだけでCMSIS-DAP v2のインターフェースにWinUSBドライバが割り当てられる。Web版もOpenOCDもこのWinUSB経由でデバッガを使う。デバイスマネージャーで「ユニバーサル シリアル バス デバイス」の下に `Debugprobe on Pico (CMSIS-DAP)` が出ていればよい（同時にCOMポートも1つ増えるが、使わない）。

デバッガが見つからないときは、まずUSBケーブル（充電専用でないもの）と、RP2040-Zeroにdebugprobeファームが書き込まれているか（[debugger_connection.md](debugger_connection.md) の「デバッガの作り方」）を確認する。

## 5. PlatformIOとGitを入れる（ビルドとコマンドライン版を使う場合）

PlatformIOはビルドに使う。初めてビルドするとき、AT32用のプラットフォーム（GitHubの `ArteryTek/platform-arterytekat32`）、ARMのツールチェーン、ArteryTek版OpenOCDが `~/.platformio/` に自動でインストールされる。プラットフォームはGitで取得するので、Gitも必要。

**macOS**：

```sh
brew install platformio git
```

**Windows**：

1. Gitを入れる：[git-scm.com](https://git-scm.com/download/win) のインストーラか `winget install Git.Git`
2. PlatformIOを入れる。次のどちらかでよい
   - VS Codeの拡張機能「PlatformIO IDE」を入れる。`pio` は `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` に入り、PATHに入っていなくても `tools\win\*.bat` が見つける
   - `py -3 -m pip install platformio`

インストールしたら、ツールのビルドを一度実行する。どちらのOSでも、Web版の起動がビルドを兼ねる。

```sh
./tools/mac/web_flasher.command      # macOS（DOOMとALIEN RAIDをビルドしてからページを開く）
tools\win\web_flasher.bat            # Windows
```

ビルドが通れば、`tools/web/firmware/` のファームが作り直される。OpenOCDもこのときにインストールされる。W25Q32読み書き用ファームなど、ほかの環境も含めて揃えるときは、`software/invaders` で `pio run -e sflash` も実行する。OpenOCDの詳細（標準版が使えない理由、インストールの確認）は [openocd_setup.md](openocd_setup.md) を参照。

PlatformIOがないと、`web_flasher` はビルドに失敗したと表示したあと、リポジトリに入っているファームでページを開く（`--no-build` を付ければビルドを試さない）。

## 6. 動作を確認する

1. `web_flasher` でページを開く
2. 「デバッガを選ぶ」でdebugprobeを選ぶ
3. PWRで基板の電源を入れ、「接続を確認」を押す。「OK: AT32F415に接続できた」と出ればよい

コマンドライン版は `./tools/mac/check_probe.command`（Windowsでは `tools\win\check_probe.bat`）で確認できる。接続できないときの原因は [debugger_connection.md](debugger_connection.md) の「接続確認」の表を参照。

## Linuxの場合

`tools/mac/*.command` はLinuxでもそのまま動く（中身はシェルスクリプト）。WebUSBとOpenOCDでデバッガを使うには、一般ユーザーにUSBデバイスへのアクセス権を与えるudevルールが必要。

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000c", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/99-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```
