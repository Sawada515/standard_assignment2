#!/usr/bin/python3
import socket
import cv2
import numpy as np
import threading
import time
from queue import Queue, Empty

# --- 設定 ---
BIND_IP = "0.0.0.0"
PORT_TOP = 50000
PORT_BOTTOM = 50001
BUFFER_SIZE = 65535

DISPLAY_FPS = 30
DISPLAY_INTERVAL = 1.0 / DISPLAY_FPS

running = True

# UDP → JPEGバイト列（常に最新1枚）
raw_queues = {
    PORT_TOP: Queue(maxsize=1),
    PORT_BOTTOM: Queue(maxsize=1),
}

# JPEG → デコード済み画像（常に最新1枚）
frame_queues = {
    PORT_TOP: Queue(maxsize=1),
    PORT_BOTTOM: Queue(maxsize=1),
}

# 分割パケット再構成用
frame_buffers = {
    PORT_TOP: bytearray(),
    PORT_BOTTOM: bytearray(),
}


def udp_listener(port, name):
    global running

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.settimeout(1.0)

    try:
        sock.bind((BIND_IP, port))
        print(f"[{name}] Listening on UDP {port}")

        while running:
            try:
                data, _ = sock.recvfrom(BUFFER_SIZE)
                if len(data) < 2:
                    continue

                flag = data[0]
                payload = data[1:]

                frame_buffers[port].extend(payload)

                if flag == 1:
                    # 完成したJPEGをraw_queueへ
                    jpeg_data = bytes(frame_buffers[port])
                    frame_buffers[port].clear()

                    q = raw_queues[port]
                    if q.full():
                        q.get_nowait()
                    q.put_nowait(jpeg_data)

            except socket.timeout:
                continue
            except Exception as e:
                print(f"[{name}] UDP Error:", e)
                frame_buffers[port].clear()

    finally:
        sock.close()


def decode_worker(port, name):
    global running

    while running:
        try:
            jpeg_data = raw_queues[port].get(timeout=0.5)
            np_data = np.frombuffer(jpeg_data, dtype=np.uint8)
            frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)

            if frame is not None:
                q = frame_queues[port]
                if q.full():
                    q.get_nowait()
                q.put_nowait(frame)
            else:
                print(f"[{name}] Decode failed")

        except Empty:
            continue
        except Exception as e:
            print(f"[{name}] Decode Error:", e)


def main():
    global running

    print("Starting Receiver...")

    threads = []

    # UDP受信スレッド
    threads.append(threading.Thread(
        target=udp_listener,
        args=(PORT_TOP, "Top View"),
        daemon=True
    ))
    threads.append(threading.Thread(
        target=udp_listener,
        args=(PORT_BOTTOM, "Bottom View"),
        daemon=True
    ))

    # デコードスレッド
    threads.append(threading.Thread(
        target=decode_worker,
        args=(PORT_TOP, "Top View"),
        daemon=True
    ))
    threads.append(threading.Thread(
        target=decode_worker,
        args=(PORT_BOTTOM, "Bottom View"),
        daemon=True
    ))

    for t in threads:
        t.start()

    last_display = 0.0

    try:
        while True:
            now = time.time()
            if now - last_display >= DISPLAY_INTERVAL:
                if not frame_queues[PORT_TOP].empty():
                    frame = frame_queues[PORT_TOP].get_nowait()
                    cv2.imshow("Top View", frame)

                if not frame_queues[PORT_BOTTOM].empty():
                    frame = frame_queues[PORT_BOTTOM].get_nowait()
                    cv2.imshow("Bottom View", frame)

                last_display = now

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        pass

    running = False
    time.sleep(0.5)
    cv2.destroyAllWindows()
    print("Receiver stopped.")


if __name__ == "__main__":
    main()
