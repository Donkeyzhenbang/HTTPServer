#!/usr/bin/env python3
# HDR Multi-Exposure Fusion Inference Worker
# Supports: LFM_V1 (brightness fusion) + CRM_V1 (color restoration)

import sys
import os
import zmq
import json
import base64
import cv2
import numpy as np
import time
import torch
from torchvision import transforms

# Add kernel path for model imports
sys.path.insert(0, '/home/jym/python/hdr-net-start')
from kernel.models import CRM_V1, LFM_V1
from kernel.utils import transFunc_RGBToYCbCr, transFunc_YCbCrToRGB, transFunc_RGBToY

PORT = 50055

# Global model instances
mDEM = None  # LFM_V1 - Brightness fusion model
mCEM = None  # CRM_V1 - Color restoration model

def load_models():
    """Load HDR models into memory"""
    global mDEM, mCEM

    print("Loading HDR models...")
    mDEM = LFM_V1()
    mCEM = CRM_V1()

    # Model checkpoints path
    ckpt_dem_path = "/home/jym/python/hdr-net-start/checkpoints/LFM_V1_Supervised_smy.pt"
    ckpt_cem_path = "/home/jym/python/hdr-net-start/checkpoints/CRM_V1_Supervised_smy.pt"

    # Load weights - map to CPU for compatibility
    ckpt_DEM = torch.load(ckpt_dem_path, map_location=torch.device('cpu'))
    mDEM.load_state_dict(ckpt_DEM['state_dict'])

    ckpt_CEM = torch.load(ckpt_cem_path, map_location=torch.device('cpu'))
    mCEM.load_state_dict(ckpt_CEM['state_dict'])

    mDEM.eval()
    mCEM.eval()

    print("HDR models loaded successfully!")
    print(f"  - LFM_V1 (Brightness Fusion): {ckpt_dem_path}")
    print(f"  - CRM_V1 (Color Restoration): {ckpt_cem_path}")

def resize(img, ratio=1):
    """Resize image"""
    new_width = int(img.shape[1] / ratio)
    new_height = int(img.shape[0] / ratio)
    return cv2.resize(img, (new_width, new_height), interpolation=cv2.INTER_LINEAR)

def preprocess_dual(img1_bytes, img2_bytes):
    """Preprocess dual exposure images for HDR inference

    Args:
        img1_bytes: Low exposure image bytes
        img2_bytes: High exposure image bytes

    Returns:
        tuple: (img1_tensor, img2_tensor)
    """
    # Decode images
    img_1 = cv2.imdecode(np.frombuffer(img1_bytes, np.uint8), cv2.IMREAD_COLOR)
    img_2 = cv2.imdecode(np.frombuffer(img2_bytes, np.uint8), cv2.IMREAD_COLOR)

    if img_1 is None or img_2 is None:
        return None, None

    # Resize if needed
    img_1 = resize(img_1)
    img_2 = resize(img_2)

    # Convert BGR to RGB
    img_1 = cv2.cvtColor(img_1, cv2.COLOR_BGR2RGB)
    img_2 = cv2.cvtColor(img_2, cv2.COLOR_BGR2RGB)

    # Normalize to [0, 1]
    img_1 = img_1.astype(np.float32) / 255.0
    img_2 = img_2.astype(np.float32) / 255.0

    # Convert to tensor
    trans = transforms.ToTensor()
    img_1 = trans(img_1).unsqueeze(0)
    img_2 = trans(img_2).unsqueeze(0)

    return img_1, img_2

def inference_hdr(img1_tensor, img2_tensor):
    """Run HDR inference

    Args:
        img1_tensor: Low exposure tensor [1, 3, H, W]
        img2_tensor: High exposure tensor [1, 3, H, W]

    Returns:
        numpy array: HDR output image [H, W, 3] in RGB format
    """
    global mDEM, mCEM

    with torch.no_grad():
        # Convert to YCbCr space
        Img1_YCbCr = transFunc_RGBToYCbCr(img1_tensor)
        Img2_YCbCr = transFunc_RGBToYCbCr(img2_tensor)

        # Extract Y channel
        Imgs0 = transFunc_RGBToY(img1_tensor)
        Imgs1 = transFunc_RGBToY(img2_tensor)

        # Brightness fusion
        mDEM_Input = torch.cat((Imgs0, Imgs1), dim=1)
        Output_Yf = mDEM(mDEM_Input)

        # Normalize output Y
        Output_Yf = (Output_Yf - torch.min(Output_Yf)) / (torch.max(Output_Yf) - torch.min(Output_Yf))

        # Color restoration
        mCEM_Input = torch.cat((Img1_YCbCr, Img2_YCbCr, Output_Yf), dim=1)
        Output_CbCr = mCEM(mCEM_Input)

        # Combine Y and CbCr to final RGB
        Output_HDR = transFunc_YCbCrToRGB(torch.cat((Output_Yf, Output_CbCr), dim=1))

        # Convert to numpy [H, W, C] in RGB format
        output = Output_HDR.squeeze(0).permute(1, 2, 0).numpy()
        output = (output * 255).clip(0, 255).astype(np.uint8)

        return output

def tensor_to_bytes(tensor_img):
    """Convert tensor image to JPEG bytes"""
    # Tensor to numpy [H, W, C]
    if len(tensor_img.shape) == 4:
        tensor_img = tensor_img.squeeze(0)
    img_np = tensor_img.permute(1, 2, 0).numpy()
    img_np = (img_np * 255).clip(0, 255).astype(np.uint8)
    img_bgr = cv2.cvtColor(img_np, cv2.COLOR_RGB2BGR)
    _, buffer = cv2.imencode('.jpg', img_bgr)
    return buffer.tobytes()

def main():
    # Load models first
    load_models()

    # Setup ZMQ
    context = zmq.Context()
    socket = context.socket(zmq.REP)
    socket.bind(f"tcp://*:{PORT}")

    print(f"HDR ZMQ Worker listening on port {PORT}...")

    while True:
        try:
            # Receive multipart message
            message = socket.recv_multipart()
            if len(message) < 2:
                socket.send_json({"error": "Expected multipart message with metadata and images"})
                continue

            meta = json.loads(message[0].decode('utf-8'))
            model_type = meta.get("model", "hdr")

            start_time = time.time()
            detections = []

            if model_type == "hdr":
                # HDR mode: expects 2 images (low exposure, high exposure)
                if len(message) < 3:
                    # Only got one image, use it for both (simulate dual exposure)
                    img_bytes = message[1]
                    img1_tensor, img2_tensor = preprocess_dual(img_bytes, img_bytes)
                else:
                    img1_bytes = message[1]
                    img2_bytes = message[2]
                    img1_tensor, img2_tensor = preprocess_dual(img1_bytes, img2_bytes)

                if img1_tensor is None:
                    socket.send_json({"error": "Failed to decode images"})
                    continue

                # Run HDR inference
                output = inference_hdr(img1_tensor, img2_tensor)

                # Convert to JPEG
                output_bgr = cv2.cvtColor(output, cv2.COLOR_RGB2BGR)
                _, buffer = cv2.imencode('.jpg', output_bgr)
                encoded_img = base64.b64encode(buffer).decode('utf-8')

                resp = {
                    "status": "success",
                    "model_used": "hdr_fusion",
                    "inference_time_ms": int((time.time() - start_time) * 1000),
                    "detections": detections,
                    "image_b64": f"data:image/jpeg;base64,{encoded_img}"
                }
            else:
                # Pass through to mock processing for other model types
                img_bytes = message[1]
                nparr = np.frombuffer(img_bytes, np.uint8)
                img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                if img is None:
                    socket.send_json({"error": "Failed to decode image"})
                    continue

                if model_type == 'yolo':
                    h, w, _ = img.shape
                    x1, y1 = int(w*0.2), int(h*0.2)
                    x2, y2 = int(w*0.8), int(h*0.8)
                    cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 3)
                    cv2.putText(img, "HDR Result", (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)
                    detections.append({"class": "object", "confidence": 0.99, "bbox": [x1, y1, x2, y2]})
                elif model_type == 'segmentation':
                    mask = np.zeros_like(img)
                    cv2.circle(mask, (img.shape[1]//2, img.shape[0]//2), 100, (0, 255, 0), -1)
                    img = cv2.addWeighted(img, 1.0, mask, 0.5, 0)

                _, buffer = cv2.imencode('.jpg', img)
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
            import traceback
            traceback.print_exc()
            socket.send_json({"error": str(e)})

if __name__ == '__main__':
    main()
