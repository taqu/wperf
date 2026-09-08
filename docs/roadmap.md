# wperf Release Roadmap

## Phase 0 — 現状棚卸しとリリース基準の固定

まずコードを触る前に、現在の状態を明文化します。

確認対象は以下です。

* ビルド方法
* 対応 Windows バージョン
* コンパイラ / SDK 要件
* CMake か Visual Studio Solution か
* 外部依存
* 実行時依存 DLL
* 設定ファイル保存場所
* 管理者権限の要否
* 現在の機能一覧
* known issues
* ライセンス
* バージョニング方式

ここで最初の公開リリースを例えば、

```text
v0.1.0
```

と定義します。

また、

```text
v0.1.0 = 現在の wperf + Lock Inspector + CI + Documentation
```

のように、リリーススコープを固定します。

### Deliverables

```text
docs/
  architecture.md
  release-policy.md
  supported-platforms.md
```

この Phase では機能追加をしません。

---

# Phase 1 — ビルドの再現性確立

GitHub Actions を作る前に、

> クリーンな Windows 環境からコマンドだけでビルドできる

状態にします。

例えば CMake なら、

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

だけで生成できる状態です。

Visual Studio 固有の手作業や IDE 設定への依存を取り除きます。

可能なら、

```text
build/
dist/
```

をソースツリーから完全に分離します。

### 検証

クリーン clone から、

```powershell
git clone ...
cd wperf
cmake ...
cmake --build ...
```

で成功すること。

### Deliverables

* reproducible build
* `.gitignore`
* dependency documentation
* build instructions

---

# Phase 2 — README の整備

この段階で GitHub のトップページを完成させます。

README は最低限、

```text
# wperf

Screenshot

## Features

## Requirements

## Installation

## Usage

## Lock Inspector

## Building from Source

## Configuration

## License
```

くらいあれば十分です。

今回のスクリーンショットも README のトップにかなり使いやすいです。

機能一覧は例えば、

```text
- RAM usage
- CPU usage
- GPU monitoring
- Disk I/O
- Network I/O
- Lightweight tray application
- On-demand locked-file/process inspection
```

程度。

特に、

> wperf aims to keep its idle resource usage minimal.

という設計方針を README に明記しておくと、今後の機能追加判断の基準にもなります。

---

# Phase 3 — 基本 CI

ここで GitHub Actions を導入します。

まずは PR / push ごとに、

```text
Checkout
   ↓
Configure
   ↓
Build Debug
   ↓
Build Release
   ↓
Tests
```

です。

例えば、

```text
.github/
  workflows/
    ci.yml
```

を作ります。

対象は最初は Windows x64 のみでよいでしょう。

```text
windows-latest
MSVC
x64
Debug
Release
```

Windows 専用ツールなので、無理に Linux/macOS CI を追加する必要はありません。

### CI failure policy

最低でも、

* compiler error
* linker error
* unit test failure

ではマージ不可にします。

---

# Phase 4 — 静的検査と警告レベル強化

CI が安定したら品質チェックを追加します。

MSVC ならまず、

```text
/W4
/permissive-
```

あたりが候補です。

いきなり `/WX` にすると既存警告の整理が大仕事になる可能性があるので、

```text
Phase 4A
/W4

Phase 4B
warning cleanup

Phase 4C
/WX
```

くらいに分けるのがおすすめです。

必要なら、

```text
clang-format
clang-tidy
```

も追加できます。

ただし wperf が小規模なら、最初のリリースに clang-tidy を必須化する必要まではないでしょう。

---

# Phase 5 — テスト基盤

パフォーマンスモニター本体は OS や GPU に依存するので、GUI を丸ごとテストしようとすると大変です。

それより内部ロジックを分離します。

例えば、

```text
src/
  monitoring/
  lock_inspector/
  platform/
  ui/
```

として、

```text
tests/
  lock_inspector_tests.cpp
  path_tests.cpp
  formatting_tests.cpp
```

のようにします。

特に Lock Inspector では、

* path normalization
* child-path判定
* duplicate process removal
* PID情報
* error handling

などを unit test できます。

OS API 自体は integration test に分けます。

---

# Phase 6 — Lock Inspector Core

ここから新機能です。

まず GUI を作らず、

```cpp
LockInspector
```

という内部 API を作ります。

概念的には、

```cpp
struct LockingProcess {
    DWORD pid;
    std::wstring process_name;
    std::vector<std::wstring> resources;
};

std::vector<LockingProcess>
FindLockingProcesses(const std::filesystem::path& path);
```

程度。

最初のバックエンドは Restart Manager。

```text
RmStartSession
RmRegisterResources
RmGetList
RmEndSession
```

を使用します。

### この Phase の完成条件

CLIまたはテストコードから、

```text
C:\foo\bar.dll
```

を指定して locker PID を取得できること。

---

# Phase 7 — Lock Inspector CLI

GUIより先に CLI を入れます。

例えば、

```powershell
wperf.exe --lock "C:\project\build"
```

出力：

```text
PID     Process
8420    devenv.exe
14324   cmake.exe
```

できれば、

```powershell
wperf.exe --lock "..." --json
```

も用意しておくとテストが非常に簡単になります。

例えば、

```json
{
  "path": "C:\\project\\build",
  "processes": [
    {
      "pid": 8420,
      "name": "devenv.exe"
    }
  ]
}
```

CI で integration test を組みやすくなります。

---

# Phase 8 — Deep Handle Scan

Restart Manager で検出できないケースへの fallback を追加します。

構成は、

```text
LockInspector
   |
   +-- RestartManagerBackend
   |
   +-- NativeHandleBackend
```

くらいにしておくとよいです。

通常は、

```text
Restart Manager
```

のみ実行。

必要な場合だけ、

```text
Deep Scan
```

します。

ここでも wperf の低負荷ポリシーを明確に守ります。

**常時スキャンは禁止**です。

---

# Phase 9 — Lock Inspector GUI

ここで初めて GUI を追加します。

最小構成なら、

```text
Path:
[........................] [Browse]

Process          PID
------------------------
devenv.exe       8420
cmake.exe        14324

[Refresh] [Close Process]
```

程度。

最初は高度な Process Explorer を目指しません。

特に、

* CPU使用率
* メモリ使用量
* スレッド一覧
* DLL一覧

などは追加しない方がよいです。

機能が膨らんで Task Manager 化するのを防ぎます。

---

# Phase 10 — プロセス終了操作

安全な順に、

```text
Request Close
      ↓
Wait
      ↓
Refresh
```

を実装。

必要なら別ボタンとして、

```text
Force Terminate
```

を追加します。

Force Terminate は明示的な確認付きにします。

例えば、

```text
Terminating this process may cause unsaved data to be lost.
```

程度。

他プロセスの handle を直接閉じる `Close Handle` は、このリリースでは入れないことを推奨します。

---

# Phase 11 — Tray Menu Integration

現在の、

```text
Settings
Purge Memory
Exit
```

に、

```text
Settings
Lock Inspector...
Purge Memory
----------------
Exit
```

を追加。

ただしクリックするまで、

```text
additional thread = 0
additional polling = 0
additional timer = 0
```

を維持します。

ここは wperf の重要な acceptance criteria にしてよいです。

---

# Phase 12 — Explorer Context Menu

次に Explorer から直接呼べるようにします。

例えば、

```text
Right click folder
    ↓
Find Locking Processes
```

から、

```powershell
wperf.exe --lock-ui "%1"
```

を起動。

Explorer 内に常駐 DLL をロードする Shell Extension より、

**shell verb → wperf.exe**

を優先します。

これなら Explorer の安定性にも影響しにくいです。

---

# Phase 13 — UX / Error Handling

この段階で異常系を潰します。

例えば、

```text
Path does not exist
Access denied
Process exited during scan
Protected process
Administrator privileges required
Restart Manager unavailable
Native handle scan failed
```

など。

プロセス一覧取得中に対象プロセスが終了するのは普通なので、

```text
ERROR_INVALID_PARAMETER
ERROR_ACCESS_DENIED
process disappeared
```

などを正常系に近いものとして扱える設計にしておきます。

---

# Phase 14 — Security Review

Lock Inspector はプロセス操作をするので、ここは独立 Phase にした方がいいです。

確認対象：

* privilege escalation
* protected process
* administrator boundary
* PID reuse
* path canonicalization
* symlink/junction
* TOCTOU
* terminate confirmation
* command line quoting

特に Explorer integration の、

```text
"%1"
```

周辺は path quoting を必ず確認します。

---

# Phase 15 — Documentation Complete

ここで docs を完成させます。

最終形として、

```text
README.md
LICENSE
CHANGELOG.md
CONTRIBUTING.md

docs/
  architecture.md
  build.md
  lock-inspector.md
  troubleshooting.md
  release.md
```

程度。

`lock-inspector.md` には、

* 何が検出できるか
* Restart Manager
* Deep Scan
* 管理者権限
* Force Terminate
* limitations

を説明します。

---

# Phase 16 — Release CI

通常 CI と Release workflow を分けます。

```text
.github/workflows/
  ci.yml
  release.yml
```

Release workflow は、

```text
git tag v0.1.0
    ↓
GitHub Actions
    ↓
Release build
    ↓
Package
    ↓
SHA256
    ↓
GitHub Release
```

とします。

成果物は例えば、

```text
wperf-v0.1.0-windows-x64.zip
wperf-v0.1.0-windows-x64.zip.sha256
```

。

ZIP の中身は、

```text
wperf.exe
README.txt
LICENSE
```

程度でよいでしょう。

---

# Phase 17 — Version Information

Windows アプリなので executable の version resource も設定しておきたいです。

Explorer の Properties で、

```text
File version:
0.1.0.0

Product version:
0.1.0
```

が見えるようにします。

さらに、

```powershell
wperf.exe --version
```

で、

```text
wperf 0.1.0
```

を返すようにします。

できれば build metadata として git commit も保持します。

```text
wperf 0.1.0
commit: a17c84e
```

---

# Phase 18 — Release Candidate

ここで、

```text
v0.1.0-rc1
```

を作ります。

RC は実環境で、

* Windows 10
* Windows 11
* clean machine
* NVIDIA GPUあり
* GPUなし
* 標準ユーザー
* Administrator
* High DPI
* multi-monitor

あたりを確認。

Lock Inspector は特に、

```text
Explorer
Visual Studio
VS Code
cmd.exe
PowerShell
CMake/Ninja
```

などでフォルダを掴ませて検証するとよいです。

---

# Phase 19 — GitHub Release

RC の問題を修正した後、

```text
v0.1.0
```

タグを作成。

GitHub Release の内容は、

```text
wperf v0.1.0

Highlights

- Lightweight desktop performance monitor
- CPU / RAM / GPU / Disk / Network monitoring
- New on-demand Lock Inspector
- Explorer context-menu integration

Downloads

wperf-v0.1.0-windows-x64.zip

Requirements

Windows 10/11 x64
```

程度で十分です。

CHANGELOG も、

```markdown
## [0.1.0] - 2026-xx-xx

### Added
- Lock Inspector
- Explorer context menu
- GitHub Actions CI
- Automated release packaging

### Changed
- Improved build reproducibility

### Fixed
- ...
```

と同期させます。

---

# Phase 20 — Post-release automation

v0.1.0 の後は開発フローを固定します。

```text
feature branch
      ↓
Pull Request
      ↓
CI
      ↓
review
      ↓
main
      ↓
tag
      ↓
release.yml
      ↓
GitHub Release
```

Dependabot を使っている依存があるなら、この段階で追加してもいいでしょう。

---

## 全体像

まとめると、この順番がかなり安定しています。

```text
Foundation
Phase 0   Release scope
Phase 1   Reproducible build
Phase 2   README
Phase 3   CI
Phase 4   Compiler/static checks
Phase 5   Test infrastructure

Lock Inspector
Phase 6   Core
Phase 7   CLI
Phase 8   Deep Scan
Phase 9   GUI
Phase 10  Process control
Phase 11  Tray integration
Phase 12  Explorer integration
Phase 13  Error handling
Phase 14  Security review

Release preparation
Phase 15  Documentation
Phase 16  Release CI
Phase 17  Versioning
Phase 18  Release Candidate
Phase 19  GitHub Release
Phase 20  Post-release workflow
```
