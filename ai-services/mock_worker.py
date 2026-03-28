import http.server
import socketserver
import json
import time
import cgi

PORT = 50051

class InferenceHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        start_time = time.time()
        print(f"[{time.strftime('%H:%M:%S')}] Received inference request on {self.path}")
        
        # Parse the form data
        ctype, pdict = cgi.parse_header(self.headers['content-type'])
        if ctype == 'multipart/form-data':
            pdict['boundary'] = bytes(pdict['boundary'], "utf-8")
            fields = cgi.parse_multipart(self.rfile, pdict)
            
            # Simple mock: get the model type
            model_type = fields.get('model', ['yolo'])[0]
        else:
            model_type = 'unknown'
            
        # Simulate GPU computation (dynamic batching window wait + inference)
        time.sleep(0.15) 
        
        # Predefined mock results
        result = {
            "status": "success",
            "model_used": model_type,
            "inference_time_ms": int((time.time() - start_time) * 1000),
            "detections": []
        }
        
        if model_type == 'yolo':
            result["detections"] = [
                {"class": "person", "confidence": 0.95, "bbox": [10, 20, 100, 200]},
                {"class": "car", "confidence": 0.88, "bbox": [150, 200, 300, 400]}
            ]
        elif model_type == 'segmentation':
            result["message"] = "Segmentation mask generated."
            result["mask_url"] = "/masks/mock_mask.png"
        else:
            result["message"] = f"Processed with {model_type}"

        # Send response
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(result).encode('utf-8'))

with socketserver.TCPServer(("", PORT), InferenceHandler) as httpd:
    print(f"Mock AI Worker listening on port {PORT}")
    httpd.serve_forever()
