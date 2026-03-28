import zmq
import json
import base64
import cv2
import numpy as np
import time

PORT = 50055

def main():
    context = zmq.Context()
    socket = context.socket(zmq.REP)
    socket.bind(f"tcp://*:{PORT}")
    
    print(f"ZMQ AI Worker listening on port {PORT}...")
    
    while True:
        try:
            # Receive multipart: [JSON metadata, Binary Image part]
            message = socket.recv_multipart()
            if len(message) < 2:
                socket.send_json({"error": "Expected multipart message"})
                continue
                
            meta = json.loads(message[0].decode('utf-8'))
            img_bytes = message[1]
            
            model_type = meta.get("model", "yolo")
            
            # Decode image
            nparr = np.frombuffer(img_bytes, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            
            if img is None:
                socket.send_json({"error": "Failed to decode image"})
                continue
            
            start_time = time.time()
            
            detections = []
            if model_type == 'yolo':
                # Draw a mock bounding box
                h, w, _ = img.shape
                # draw box in center
                x1, y1 = int(w*0.2), int(h*0.2)
                x2, y2 = int(w*0.8), int(h*0.8)
                cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 3)
                cv2.putText(img, "person 0.99", (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
                detections.append({"class": "person", "confidence": 0.99, "bbox": [x1, y1, x2, y2]})
            elif model_type == 'segmentation':
                # Apply a green tint for segmentation mask simulation
                mask = np.zeros_like(img)
                cv2.circle(mask, (img.shape[1]//2, img.shape[0]//2), 100, (0, 255, 0), -1)
                img = cv2.addWeighted(img, 1.0, mask, 0.5, 0)
                
            # Encode back to JPEG
            _, buffer = cv2.imencode('.jpg', img)
            
            # Base64 encode for the frontend to consume easily within a JSON
            encoded_img = base64.b64encode(buffer).decode('utf-8')
            
            resp = {
                "status": "success",
                "model_used": model_type,
                "inference_time_ms": int((time.time() - start_time) * 1000),
                "detections": detections,
                "image_b64": f"data:image/jpeg;base64,{encoded_img}"
            }
            
            socket.send_json(resp)
            
        except Exception as e:
            print(f"Error processing message: {e}")
            socket.send_json({"error": str(e)})

if __name__ == '__main__':
    main()
