# demo — フレーム検索 UI

[English README](README.md)

受信プログラムが書き込んだ MongoDB コレクションを検索し、該当する `.jph` フレームを一覧してダウンロードできる小さな Flask アプリです。ローカル時刻の範囲で検索し、新しい順に並んだ結果を `limit` パラメータで打ち切ります。

## セットアップ

```sh
cd demo
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## 起動

受信プログラムは `.jph` ファイルを自身のカレントディレクトリ（または `archive_dir` 引数で指定したディレクトリ）に書き出します。`ARCHIVE_DIR` でデモにそのディレクトリを指し示してください。

```sh
ARCHIVE_DIR=/path/to/jph/archive .venv/bin/python app.py
```

ブラウザで http://127.0.0.1:5000/ を開きます。

環境変数による上書き（すべて任意）:

| 変数 | 既定値 | 用途 |
| --- | --- | --- |
| `HOST` | `127.0.0.1` | バインドアドレス。全インターフェースで待ち受けるなら `0.0.0.0`、特定の LAN/WAN IP を指定することも可。 |
| `PORT` | `5000` | TCP ポート |
| `DEBUG` | `1` | Flask のデバッグモード（自動リロード含む）。ループバック以外にバインドする場合は `0` にする（下記の警告参照）。 |
| `MONGODB_URI` | `mongodb://localhost:27017/` | ドライバの URI |
| `MONGODB_DB` | `test` | データベース名（`main.cpp` と一致） |
| `MONGODB_COLLECTION` | `item1` | コレクション名（`main.cpp` と一致） |
| `ARCHIVE_DIR` | `.`（カレント） | ディスク上の `.jph` ファイル置き場 |
| `DISPLAY_TZ` | _（システムのローカル）_ | フォーム入力と結果列の両方に適用される IANA タイムゾーン名（例: `Asia/Tokyo`）。サーバが UTC で動いているが別ゾーンで検索・表示したい場合に設定。 |

> **⚠ セキュリティ注意。** Flask のデバッグモードは Werkzeug の対話デバッガを公開します。ポートに到達できる相手はそこから任意の Python を実行できます。**`127.0.0.1` 以外にバインドする場合は必ず `DEBUG=0` にしてください**（例: `HOST=0.0.0.0` や LAN IP の指定時）。信頼できないネットワークに対して本格的に公開するなら、`app.py` を直接使わず、本物の WSGI サーバ（gunicorn、uWSGI 等）の後ろで動かしてください。

LAN に公開する例:

```sh
HOST=0.0.0.0 DEBUG=0 ARCHIVE_DIR=/path/to/jph .venv/bin/python app.py
```

## プレビュー（TODO）

結果行は現在、メタデータとダウンロードリンクのみを表示します。UI 側の `Preview` 列にはプレースホルダと、[OpenHTJ2K](https://github.com/osamu620/OpenHTJ2K) の wasm デコーダ（参考実装: <https://htj2k-demo.pages.dev>）への統合方針を記したコメントが入っています。`/file/<fname>` エンドポイントが既に生の `.jph` バイト列を返すので、あとはフロントエンド側の作業だけです — wasm モジュールを一度ロードし、各行のバイト列を取得してデコードし、`<canvas>` に描画する流れになります。

## 補足

- ファイル名のバリデーションは受信プログラムの `YYYY-MM-DD-HH-MM-SS.mmm.jph` パターンに合致しないものを弾くため、`/file/` でのパストラバーサルを防いでいます。
- Mongo クライアントは `tz_aware=True` で生成しているため、タイムスタンプは UTC 付きの `datetime` オブジェクトとして取得されます。ルートはこれを表示用にだけローカル時刻へ変換します。
