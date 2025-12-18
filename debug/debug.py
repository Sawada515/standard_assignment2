#!/usr/bin/python3
import socket
import cv2
import numpy as np
import threading
import time
from queue import Queue, Empty

# --- 設定 ---
BIND_IP = "0.0.0.0"
PORT = 50000          # 受信するポート番号（1つのみ）
BUFFER_SIZE = 65535

DISPLAY_FPS = 30
DISPLAY_INTERVAL = 1.0 / DISPLAY_FPS
WINDOW_NAME = "Video Stream"

running = True

# UDP → JPEGバイト列（常に最新1枚）
raw_queue = Queue(maxsize=1)

# JPEG → デコード済み画像（常に最新1枚）
frame_queue = Queue(maxsize=1)

# 分割パケット再構成用バッファ
frame_buffer = bytearray()


def udp_listener():
    """UDPパケットを受信し、JPEGデータを再構成するスレッド"""
    global running
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # 受信バッファを大きめに設定
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.settimeout(1.0)

    try:
        sock.bind((BIND_IP, PORT))
        print(f"[UDP] Listening on port {PORT}")

        while running:
            try:
                data, _ = sock.recvfrom(BUFFER_SIZE)
                if len(data) < 2:
                    continue

                # 先頭1バイトをフラグとして使用
                flag = data[0]
                payload = data[1:]

                # バッファに追記
                frame_buffer.extend(payload)

                # フラグが1ならフレーム終了（JPEG完成）
                if flag == 1:
                    jpeg_data = bytes(frame_buffer)
                    frame_buffer.clear()

                    # 最新フレームのみをキューに入れる（古いのは捨てる）
                    if raw_queue.full():
                        raw_queue.get_nowait()
                    raw_queue.put_nowait(jpeg_data)

            except socket.timeout:
                continue
            except Exception as e:
                print(f"[UDP] Error: {e}")
                frame_buffer.clear()

    finally:
        sock.close()


def decode_worker():
    """受信したJPEGデータをデコードするスレッド"""
    global running

    while running:
        try:
            # キューからJPEGデータを取得
            jpeg_data = raw_queue.get(timeout=0.5)
            
            # デコード処理
            np_data = np.frombuffer(jpeg_data, dtype=np.uint8)
            frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)

            if frame is not None:
                # デコード済み画像をキューへ
                if frame_queue.full():
                    frame_queue.get_nowait()
                frame_queue.put_nowait(frame)
            else:
                print("[Decode] Failed to decode image")

        except Empty:
            continue
        except Exception as e:
            print(f"[Decode] Error: {e}")


def main():
    global running

    print("Starting Receiver (Single Stream)...")

    threads = []

    # 1. UDP受信スレッド起動
    t_udp = threading.Thread(target=udp_listener, daemon=True)
    threads.append(t_udp)
    t_udp.start()

    # 2. デコードスレッド起動
    t_dec = threading.Thread(target=decode_worker, daemon=True)
    threads.append(t_dec)
    t_dec.start()

    last_display = 0.0

    try:
        while True:
            now = time.time()
            # 表示更新レートの制御
            if now - last_display >= DISPLAY_INTERVAL:
                if not frame_queue.empty():
                    frame = frame_queue.get_nowait()
                    cv2.imshow(WINDOW_NAME, frame)
                
                last_display = now

            # 'q'キーで終了
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        pass

    # 終了処理
    running = False
    print("Stopping threads...")
    time.sleep(0.5) # スレッドの終了を少し待つ
    cv2.destroyAllWindows()
    print("Receiver stopped.")


if __name__ == "__main__":
    main()
    