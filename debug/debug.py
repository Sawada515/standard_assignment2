#!/usr/bin/python3

import socket
import cv2
import numpy as np

# --- 設定 ---
# 受信側(このPC)の設定
# "0.0.0.0" は「すべてのネットワークインターフェースで待機」という意味です
BIND_IP = "0.0.0.0"

# C++側(送信側)で設定したポート番号と同じにしてください
BIND_PORT = 50000

# UDPの最大パケットサイズ (64KB - IPヘッダ等)
# 画像サイズがこれを超える場合は、送信側で分割するか圧縮率を下げる必要があります
BUFFER_SIZE = 65535

def main():
    # UDPソケットの作成
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # ソケットをアドレスとポートに紐付け(バインド)
        sock.bind((BIND_IP, BIND_PORT))
        print(f"Waiting for UDP stream on port {BIND_PORT}...")
        print("Press 'q' to exit.")

        while True:
            # 1. データを受信 (data: バイト列, addr: 送信元アドレス)
            data, addr = sock.recvfrom(BUFFER_SIZE)
            
            # 2. 受信したバイト列をnumpy配列(uint8)に変換
            # これはまだ画像ではなく、ただの「数値の羅列」です
            np_data = np.frombuffer(data, dtype=np.uint8)
            
            # 3. バイナリデータ(JPEG等)をデコードして画像(Mat形式)にする
            frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)
            
            # 4. 表示
            if frame is not None:
                cv2.imshow('UDP Receiver', frame)
            else:
                print("Received data, but failed to decode image.")

            # 5. 'q' キーが押されたら終了
            # waitKey(1) は1ms待機という意味。これがないとウィンドウが描画されません
            if cv2.waitKey(1) & 0xFF == ord('q'):
                print("Exiting...")
                break

    except KeyboardInterrupt:
        print("\nStopped by user.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        # 終了処理
        sock.close()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()