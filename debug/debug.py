#!/usr/bin/python3
import socket
import cv2
import numpy as np
import threading
import time

# --- 設定 ---
BIND_IP = "0.0.0.0"
PORT_TOP = 50000
PORT_BOTTOM = 50001
BUFFER_SIZE = 65535 # 受信バッファサイズ

latest_frames = {
    PORT_TOP: None,
    PORT_BOTTOM: None
}
running = True

# 分割パケットを一時保存するバッファ
frame_buffers = {
    PORT_TOP: bytearray(),
    PORT_BOTTOM: bytearray()
}

def udp_listener(port, window_name):
    global running, latest_frames, frame_buffers

    # ソケット作成
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # OSの受信バッファを拡張 (重要)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024) 
    sock.settimeout(1.0) 
    
    try:
        sock.bind((BIND_IP, port))
        print(f"[{window_name}] Listening on port {port}...")

        while running:
            try:
                # 受信
                data, addr = sock.recvfrom(BUFFER_SIZE)

                if len(data) < 2:
                    continue

                # 1. ヘッダー(1byte) と ペイロード(残り) に分離
                flag = data[0]
                payload = data[1:]

                # 2. バッファに追記
                frame_buffers[port].extend(payload)

                # 3. フラグが 1 (終了) ならデコードを試みる
                if flag == 1:
                    np_data = np.frombuffer(frame_buffers[port], dtype=np.uint8)
                    frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)

                    if frame is not None:
                        latest_frames[port] = frame
                    else:
                        # 分割パケットの一部が欠損するとここに来ます
                        print(f"[{window_name}] Decode Error (Packet Loss?)")

                    # バッファをリセット (次のフレーム用)
                    frame_buffers[port] = bytearray()

            except socket.timeout:
                continue
            except Exception as e:
                print(f"[{window_name}] Error: {e}")
                frame_buffers[port] = bytearray()

    except Exception as e:
        print(f"[{window_name}] Bind Error: {e}")
    finally:
        sock.close()

# ... (main関数は以前のまま、表示ループのみ) ...
def main():
    global running
    print("Starting Receiver...")
    
    thread_top = threading.Thread(target=udp_listener, args=(PORT_TOP, "Top View"), daemon=True)
    thread_bottom = threading.Thread(target=udp_listener, args=(PORT_BOTTOM, "Bottom View"), daemon=True)

    thread_top.start()
    thread_bottom.start()

    try:
        while True:
            if latest_frames[PORT_TOP] is not None:
                cv2.imshow("Top View", latest_frames[PORT_TOP])
            if latest_frames[PORT_BOTTOM] is not None:
                cv2.imshow("Bottom View", latest_frames[PORT_BOTTOM])

            if cv2.waitKey(1) & 0xFF == ord('q'):
                running = False
                break
            time.sleep(0.001)
    except KeyboardInterrupt:
        running = False
    
    thread_top.join()
    thread_bottom.join()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
    