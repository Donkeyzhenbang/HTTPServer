import sys
import time
import threading
import http.client
import queue

# Config
HOST = "127.0.0.1"
PORT = 8080
PATH = "/health"
THREADS = 50
DURATION = 10

success_count = 0
fail_count = 0
running = True
lock = threading.Lock()

def worker():
    global success_count, fail_count
    conn = http.client.HTTPConnection(HOST, PORT, timeout=2)
    
    while running:
        try:
            conn.request("GET", PATH)
            resp = conn.getresponse()
            resp.read() # Consume
            if resp.status == 200:
                with lock:
                    success_count += 1
            else:
                with lock:
                    fail_count += 1
        except Exception as e:
            with lock:
                fail_count += 1
            # Reconnect
            conn.close()
            conn = http.client.HTTPConnection(HOST, PORT, timeout=2)
            time.sleep(0.1)
    
    conn.close()

def main():
    global running, success_count, fail_count
    
    print(f"Starting HTTP QPS Test against {HOST}:{PORT}{PATH} with {THREADS} threads for {DURATION}s...")
    
    threads = []
    for _ in range(THREADS):
        t = threading.Thread(target=worker)
        t.daemon = True
        t.start()
        threads.append(t)
        
    start_time = time.time()
    
    while time.time() - start_time < DURATION:
        time.sleep(1)
        with lock:
            sc = success_count
            fc = fail_count
            success_count = 0
            fail_count = 0
        print(f"HTTP QPS: {sc} req/s (Failures: {fc})")
        
    running = False
    print("Test Finished.")

if __name__ == "__main__":
    main()
