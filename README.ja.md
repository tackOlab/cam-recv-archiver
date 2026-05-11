# cam-recv-archiver

[English README](README.md)

HTJ2K ベースのインテリジェントカメラから映像を受信し、ローカルディスクに保存しつつ MongoDB にインデックスを作成する TCP 受信プログラムです。**Ubuntu 24.04 LTS**、g++ 13、CMake 3.28 で開発・動作確認しています。

```
   カメラ (HTJ2K コードストリーム)         このリポジトリ
   ┌──────────────────────┐               ┌────────────────────────────────┐
   │ 長さプレフィックス付き  │   TCP :4001   │ recv                           │
   │ TCP フレーム           │ ────────────► │  ├── コードストリームを JPH     │
   └──────────────────────┘               │  │   コンテナに格納              │
                                          │  ├── <timestamp>.jph として保存 │ ──► <archive_dir>
                                          │  └── {fname,timestamp} を挿入  │ ──► mongod (test.item1)
                                          │                                │
                                          │ demo/  (Flask)                 │
                                          │  └── インデックスを範囲検索し、 │ ◄── HTTP :5000
                                          │      フレームをダウンロード/表示 │
                                          └────────────────────────────────┘
```

## リポジトリ構成

| パス | 役割 |
| --- | --- |
| `main.cpp` | 受信プログラム本体: TCP サーバ + JPH ラップ + Mongo への挿入 |
| `simple_tcp.hpp` | 長さプレフィックス付きフレーミングを行うヘッダオンリーの TCP ラッパー |
| `CMakeLists.txt` | ビルド定義 — `find_package(mongocxx CONFIG REQUIRED)` |
| `demo/` | アーカイブをタイムスタンプ範囲で検索する Flask アプリ |

---

## 環境構築（ゼロから）

クリーンな Ubuntu 24.04 環境を、ビルドが通る状態まで持っていく手順です。上から順に実行してください。

### 0. apt を最新化する

```sh
sudo apt update
```

### 1. ビルドツールチェーンと依存ライブラリを導入する

```sh
sudo apt install -y build-essential cmake pkg-config \
                    libssl-dev libsasl2-dev libmongocrypt-dev \
                    git curl
```

それぞれの用途:

| パッケージ | 用途 |
| --- | --- |
| `build-essential` | g++、make、libc ヘッダ |
| `cmake`, `pkg-config` | ビルドの構成 |
| `libssl-dev` | Mongo ドライバの TLS サポート |
| `libsasl2-dev` | Mongo ドライバの SASL 認証 |
| `libmongocrypt-dev` | mongo-cxx-driver が要求するクライアントサイド暗号化サポート |
| `git`, `curl` | ドライバの取得と MongoDB リポジトリの鍵取得 |

導入確認:

```sh
g++ --version    # 13.x 系であること
cmake --version  # 3.15 以上であること
```

### 2. システムのタイムゾーンを設定する（任意・推奨）

`recv` はファイル名をシステムのローカル時刻から組み立て、デモ画面の日時範囲フォームも `DISPLAY_TZ` が未設定なら入力をシステムのローカル時刻として解釈します。最初に一つのタイムゾーンを決めておくと、あとで混乱しません。

```sh
sudo timedatectl set-timezone Asia/Tokyo   # 必要に応じて変更
date                                       # 確認
```

システムを UTC のまま運用し、デモの表示タイムゾーンだけを `DISPLAY_TZ` で切り替えることもできます（`demo/README.md` 参照）。ただしファイル名は引き続きシステムのローカル時刻になります。

### 3. MongoDB サーバを導入する

Ubuntu 同梱の `mongodb` パッケージは保守されていないため、MongoDB 公式の apt リポジトリを利用してください。リポジトリの鍵追加・sources.list 追加・メタパッケージのインストール手順は上流ドキュメントに従います。

→ <https://www.mongodb.com/docs/manual/tutorial/install-mongodb-on-ubuntu/>

インストール後、起動と疎通確認:

```sh
sudo systemctl enable --now mongod
systemctl is-active mongod                       # → active
mongosh --quiet --eval 'db.runCommand({ping:1})' # → { ok: 1 }
```

`recv` は認証なしで `mongodb://localhost:27017/` に接続します。

### 4. mongo-c-driver をソースからビルド・インストールする

`mongocxx` 4.x は `libmongoc` 1.28 以上を要求しますが、apt 版は古いためソースからビルドします。**1.29.0** で動作確認済み。

```sh
git clone --depth 1 --branch 1.29.0 \
    https://github.com/mongodb/mongo-c-driver.git
cd mongo-c-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
cd ..
```

デフォルトで `/usr/local` 配下にインストールされます。C++ ドライバの CMake 設定ファイルが `find_package` の探索範囲に置かれるという意味で、ここは重要です。

確認:

```sh
pkg-config --modversion libmongoc-1.0 libbson-1.0   # → 1.29.0
```

### 5. mongo-cxx-driver をソースからビルド・インストールする

`CMakeLists.txt` がリンクする `mongo::mongocxx_shared` / `mongo::bsoncxx_shared` ターゲットを提供するのがこの手順です。**4.0.0** で動作確認済み。

```sh
git clone --depth 1 --branch r4.0.0 \
    https://github.com/mongodb/mongo-cxx-driver.git
cd mongo-cxx-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBSONCXX_POLY_USE_IMPLS=ON
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
cd ..
```

確認:

```sh
pkg-config --modversion libmongocxx libbsoncxx     # → 4.0.0
ls /usr/local/lib/cmake/mongocxx-4.0.0/            # CMake 設定ファイル
```

---

## 受信プログラムをビルドする

リポジトリのルートで:

```sh
cmake -B build
cmake --build build -j"$(nproc)"
```

実行ファイル `./build/recv` が生成されます。

ドライバを `/usr/local` 以外に入れた場合は、configure 時に `-DCMAKE_PREFIX_PATH=/your/prefix` を渡してください。

## 受信プログラムを起動する

```sh
./build/recv [archive_dir]
```

- `archive_dir`（省略可）は `.jph` ファイルの保存先ディレクトリです。存在しなければ自動的に作成されます。省略時はカレントディレクトリ。
- 受信プログラムは **TCP 4001** で待ち受け、準備完了時に `Awaiting connection on :4001 ...` を表示します。
- フレーム受信ごとに `<filename> = <N> bytes` を出力し、MongoDB の `test.item1` に 1 ドキュメントを挿入します。

`libmongocxx.so` が見つからない（`error while loading shared libraries`）場合は、`LD_LIBRARY_PATH=/usr/local/lib` を付けて起動してください。

挿入される MongoDB ドキュメントの形は次のとおりです:

```json
{ "_id": ObjectId(...), "fname": "2026-05-11-17-17-15.123.jph", "timestamp": ISODate(...) }
```

`fname` は**ベース名のみ**を保持します。デモ側はこれをデモ自身の `ARCHIVE_DIR` と組み合わせてディスク上のファイルを解決します。

## エンドツーエンドのスモークテスト（カメラ不要）

カメラなしで通信経路と Mongo への書き込みを確認するには、長さプレフィックス付きのダミーペイロードを送ります。一方の端末で:

```sh
mkdir -p /tmp/jph-test
./build/recv /tmp/jph-test
```

別の端末で:

```sh
python3 - <<'PY'
import socket, struct, os
payload = os.urandom(4096)   # 任意のバイト列。実際のコードストリームでなくてよい
with socket.create_connection(('127.0.0.1', 4001)) as s:
    s.sendall(struct.pack('!I', len(payload)) + payload)
PY
```

`recv` 側に `…bytes` の出力が表示され、`/tmp/jph-test/` 配下に新しい `.jph` ファイルが生成され、`test.item1` に新しいドキュメントが追加されているはずです:

```sh
ls /tmp/jph-test/
mongosh --quiet --eval 'db.getSiblingDB("test").item1.find().sort({_id:-1}).limit(1)'
```

（生成された `.jph` はランダムバイトを JPH ヘッダで包んだものなので、HTJ2K 画像としては不正です。あくまで受信 → 書き込み → 挿入の経路全体を通過させるためのテストです。）

## 検索 UI（デモ）

[`demo/`](demo/) 配下の小さな Flask アプリが `test.item1` をタイムスタンプ範囲で検索し、該当する `.jph` ファイルを返します。セットアップ手順・環境変数（`HOST`、`PORT`、`ARCHIVE_DIR`、`DISPLAY_TZ` ほか）・予定している [OpenHTJ2K](https://github.com/osamu620/OpenHTJ2K) WebAssembly デコーダによるプレビュー統合の方針は [`demo/README.md`](demo/README.md) に記載しています。

---

## トラブルシューティング

| 症状 | 原因と対処 |
| --- | --- |
| `cmake` が `mongocxx` を見つけられない | 手順 4 または 5 を未実施、または `/usr/local` 以外に入っている。`sudo ldconfig` をやり直すか、`-DCMAKE_PREFIX_PATH=/your/prefix` を指定する。 |
| `./build/recv: error while loading shared libraries: libmongocxx.so...` | ローダが `/usr/local/lib` を見ていない。`sudo ldconfig`（一回でよい）を実行するか、`LD_LIBRARY_PATH=/usr/local/lib ./build/recv` で起動する。 |
| 受信プログラムが `bind/listen` を出して終了する | ポート 4001 を別プロセス（多くは前回の `recv`）が掴んでいる。`ss -ltnp '( sport = :4001 )'` で特定して終了させる。 |
| 最初のフレームで `mongocxx::exception` | `mongod` が動いていないか、デフォルト以外のポートで待っている。`systemctl status mongod` を確認する。 |
| デモの `python3 -m venv .venv` が `ensurepip is not available` で失敗する | `sudo apt install python3.12-venv` を実行する。 |
| ファイル名が 9 時間ずれる | システムのタイムゾーンが想定と異なる（手順 2 参照）。新しいファイルは `recv` 起動時点のタイムゾーンで命名されるので、タイムゾーン変更後は `recv` を再起動すること。 |
