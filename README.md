# 標準課題2 カメラモジュール
Webカメラで基板の動画を撮影・UDP送信用のプログラム

## 想定動作環境
Raspberry Pi 5<br>
Webカメラ (V4L2対応)

## 開発環境
Linux kali 6.17.10+kali-amd64 #1 SMP PREEMPT_DYNAMIC Kali 6.17.10-1kali1 (2025-12-08) x86_64 GNU/Linux

## 必要パッケージインストール
初回のみ実行<br>
apt install ...を実行します

```terminal
$ sudo bash ./script/require_pkg_install.sh
```

## コンパイル
1. ビルド用ディレクトリを作成<br>
2. CMakeでMakefileを生成<br>
3. Makefileを実行

```terminal
$ mkdir build && cd $_
$ cmake ..
$ make
```

## 実行
```terminal
$ ./bin/webcam_app
```

## ドキュメント生成
```terminal
$ doxygen
```

## TODO
実行確認<br>
コンテナで実行できるように準備する
