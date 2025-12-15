#!/usr/bin/python3
import socket
import cv2
import numpy as np
import threading
import time

# --- 設定 (YAMLの設定に合わせる) ---
BIND_IP = "0.0.0.0"

# ポート設定
PORT_TOP = 50000
PORT_BOTTOM = 50001

# バッファサイズ
BUFFER_SIZE = 65535

# 受信した最新フレームを格納する辞書 (スレッド間で共有)
# Key: Port番号, Value: 画像データ(Mat)
latest_frames = {
    PORT_TOP: None,
    PORT_BOTTOM: None
}

# プログラム終了フラグ
running = True

def udp_listener(port, window_name):
    """
    指定されたポートでUDP受信を続けるスレッド用関数
    """
    global running, latest_frames

    # ソケット作成
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # タイムアウトを設定（終了フラグを確認できるようにするため）
    sock.settimeout(1.0) 
    
    try:
        sock.bind((BIND_IP, port))
        print(f"[{window_name}] Listening on port {port}...")

        while running:
            try:
                # 1. データ受信
                data, addr = sock.recvfrom(BUFFER_SIZE)

                # 2. バイナリ -> numpy配列
                np_data = np.frombuffer(data, dtype=np.uint8)

                # 3. デコード (JPEG -> Mat)
                frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)

                # 4. 共有変数を更新 (メインスレッドが表示する)
                if frame is not None:
                    latest_frames[port] = frame
                else:
                    print(f"[{window_name}] Decode Error")

            except socket.timeout:
                # タイムアウトしたらループ先頭に戻って running を確認
                continue
            except Exception as e:
                print(f"[{window_name}] Error: {e}")

    except Exception as e:
        print(f"[{window_name}] Bind Error: {e}")
    finally:
        sock.close()
        print(f"[{window_name}] Socket closed.")

def main():
    global running

    print("Starting Dual UDP Receiver...")
    print("Press 'q' to exit.")

    # 1. 受信スレッドの作成と開始
    thread_top = threading.Thread(target=udp_listener, args=(PORT_TOP, "Top View"), daemon=True)
    thread_bottom = threading.Thread(target=udp_listener, args=(PORT_BOTTOM, "Bottom View"), daemon=True)

    thread_top.start()
    thread_bottom.start()

    # 2. メインループ (画像の表示担当)
    # OpenCVのimshowはメインスレッドで呼ぶ必要があります
    try:
        while True:
            # Top View の表示
            if latest_frames[PORT_TOP] is not None:
                cv2.imshow("Top View (Port 50000)", latest_frames[PORT_TOP])

            # Bottom View の表示
            if latest_frames[PORT_BOTTOM] is not None:
                cv2.imshow("Bottom View (Port 50001)", latest_frames[PORT_BOTTOM])

            # キー入力待機 (1ms) & 終了判定
            if cv2.waitKey(1) & 0xFF == ord('q'):
                print("Exiting...")
                running = False
                break
            
            # CPU負荷を下げるため少し待機
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("Stopped by user")
        running = False

    # 終了処理
    thread_top.join()
    thread_bottom.join()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
    