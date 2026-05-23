#!/usr/bin/env python3
# HDR Inference Engine
# LFM_V1 (brightness fusion) + CRM_V1 (color restoration)
# Note: LFM_V1 and CRM_V1 execute serially (not parallel)

import torch
import cv2
import numpy as np
import logging
import time
from typing import Optional, Tuple

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class HDREngine:
    """HDR Inference Engine

    LFM_V1 and CRM_V1 must execute serially due to data dependency:
    - LFM_V1 outputs Y channel fusion result
    - CRM_V1 takes LFM output + original YCbCr as input

    CUDA Streams are used for streaming/batching overlap, not for parallel
    execution of the two models.
    """

    # Default paths
    DEFAULT_KERNEL_PATH = '/home/jym/python/hdr-net-start'

    def __init__(self, checkpoint_dir: str, gpu_id: int = 0):
        self.device = torch.device(f'cuda:{gpu_id}')
        torch.cuda.set_device(gpu_id)
        self.gpu_id = gpu_id
        self.checkpoint_dir = checkpoint_dir

        # Model instances
        self.mDEM = None
        self.mCRM = None

        # Load models
        self._load_models()

        # Stats
        self.inference_count = 0
        self.total_inference_time = 0.0

    def _load_models(self):
        """Load HDR models to GPU"""
        # Add kernel path to sys.path if not already present
        import sys
        if self.DEFAULT_KERNEL_PATH not in sys.path:
            sys.path.insert(0, self.DEFAULT_KERNEL_PATH)

        from kernel.models import LFM_V1, CRM_V1

        logger.info(f"[GPU {self.gpu_id}] Loading LFM_V1 model...")
        self.mDEM = LFM_V1()
        ckpt_dem = torch.load(
            f"{self.checkpoint_dir}/LFM_V1_Supervised_smy.pt",
            map_location='cpu'
        )
        self.mDEM.load_state_dict(ckpt_dem['state_dict'])
        self.mDEM.to(self.device)
        self.mDEM.eval()

        logger.info(f"[GPU {self.gpu_id}] Loading CRM_V1 model...")
        self.mCRM = CRM_V1()
        ckpt_crm = torch.load(
            f"{self.checkpoint_dir}/CRM_V1_Supervised_smy.pt",
            map_location='cpu'
        )
        self.mCRM.load_state_dict(ckpt_crm['state_dict'])
        self.mCRM.to(self.device)
        self.mCRM.eval()

        logger.info(f"[GPU {self.gpu_id}] Models loaded successfully")

    def warmup(self, warmup_iterations: int = 2):
        """Warmup GPU with dummy inference"""
        logger.info(f"[GPU {self.gpu_id}] Warming up GPU...")

        dummy1 = torch.randn(1, 3, 512, 512, device=self.device)
        dummy2 = torch.randn(1, 3, 512, 512, device=self.device)

        with torch.no_grad():
            for _ in range(warmup_iterations):
                _ = self._inference_impl(dummy1, dummy2)

        torch.cuda.synchronize()
        logger.info(f"[GPU {self.gpu_id}] GPU warmup complete")

    def preprocess(self, img_bytes: bytes) -> torch.Tensor:
        """Preprocess image bytes to tensor

        Args:
            img_bytes: JPEG/PNG image bytes

        Returns:
            tensor: [1, 3, H, W] normalized to [0, 1]
        """
        # Decode image
        img = cv2.imdecode(np.frombuffer(img_bytes, np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            raise ValueError("Failed to decode image")

        # Ensure image is 3-channel color
        if len(img.shape) != 3 or img.shape[2] != 3:
            raise ValueError(f"Expected 3-channel image, got shape {img.shape}")

        # BGR to RGB
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        # Normalize to [0, 1] and convert to tensor directly
        img = img.astype(np.float32) / 255.0

        # Convert to tensor [3, H, W] then add batch dim [1, 3, H, W]
        tensor = torch.from_numpy(img.transpose(2, 0, 1)).unsqueeze(0)

        return tensor.to(self.device)

    def preprocess_batch(self, img_bytes_list: list) -> torch.Tensor:
        """Preprocess multiple images to batch tensor

        Args:
            img_bytes_list: List of image bytes

        Returns:
            tensor: [N, 3, H, W] batch tensor
        """
        tensors = [self.preprocess(img) for img in img_bytes_list]
        return torch.cat(tensors, dim=0)

    def _inference_impl(self, img1_batch: torch.Tensor, img2_batch: torch.Tensor) -> torch.Tensor:
        """Internal inference implementation (serial execution)

        Args:
            img1_batch: [N, 3, H, W] low exposure images
            img2_batch: [N, 3, H, W] high exposure images

        Returns:
            output: [N, 3, H, W] HDR fused images
        """
        with torch.no_grad():
            # Step 1: LFM_V1 - Brightness fusion
            img1_y = self._extract_y(img1_batch)
            img2_y = self._extract_y(img2_batch)
            lfm_input = torch.cat([img1_y, img2_y], dim=1)  # [N, 2, H, W]
            output_y = self.mDEM(lfm_input)  # [N, 1, H, W]

            # Normalize Y output
            output_y = (output_y - output_y.min()) / (output_y.max() - output_y.min() + 1e-8)

            # Step 2: CRM_V1 - Color restoration (depends on LFM output, must be serial)
            img1_ycbcr = self._rgb_to_ycbcr(img1_batch)
            img2_ycbcr = self._rgb_to_ycbcr(img2_batch)
            crm_input = torch.cat([img1_ycbcr, img2_ycbcr, output_y], dim=1)  # [N, 7, H, W]
            output_cbcr = self.mCRM(crm_input)  # [N, 2, H, W]

            # Step 3: Combine Y and CbCr to RGB
            output = self._ycbcr_to_rgb(torch.cat([output_y, output_cbcr], dim=1))

        return output

    def infer(self, img1_batch: torch.Tensor, img2_batch: torch.Tensor) -> torch.Tensor:
        """HDR inference with timing

        Args:
            img1_batch: [N, 3, H, W] low exposure
            img2_batch: [N, 3, H, W] high exposure

        Returns:
            output: [N, 3, H, W] HDR result
        """
        start_time = time.time()

        output = self._inference_impl(img1_batch, img2_batch)

        torch.cuda.synchronize()

        elapsed = time.time() - start_time
        self.inference_count += 1
        self.total_inference_time += elapsed

        return output

    def _extract_y(self, img_rgb: torch.Tensor) -> torch.Tensor:
        """Extract Y channel (luminance) from RGB

        Y = 0.299R + 0.587G + 0.114B
        """
        Y = 0.299 * img_rgb[:, 0:1, :, :] + \
            0.587 * img_rgb[:, 1:2, :, :] + \
            0.114 * img_rgb[:, 2:3, :, :]
        return Y

    def _rgb_to_ycbcr(self, img_rgb: torch.Tensor) -> torch.Tensor:
        """Convert RGB to YCbCr

        Args:
            img_rgb: [N, 3, H, W] RGB tensor

        Returns:
            [N, 3, H, W] YCbCr tensor (Y, Cb, Cr)
        """
        Y = self._extract_y(img_rgb)

        # Cb = 128/256 - 0.168736*R - 0.331264*G + 0.5*B
        Cb = 0.5 - 0.168736 * img_rgb[:, 0, :, :] - \
             0.331264 * img_rgb[:, 1, :, :] + \
             0.5 * img_rgb[:, 2, :, :]

        # Cr = 128/256 + 0.5*R - 0.418688*G - 0.081312*B
        Cr = 0.5 + 0.5 * img_rgb[:, 0, :, :] - \
             0.418688 * img_rgb[:, 1, :, :] - \
             0.081312 * img_rgb[:, 2, :, :]

        return torch.stack([Y.squeeze(1), Cb, Cr], dim=1)

    def _ycbcr_to_rgb(self, img_ycbcr: torch.Tensor) -> torch.Tensor:
        """Convert YCbCr to RGB

        Args:
            img_ycbcr: [N, 3, H, W] YCbCr tensor

        Returns:
            [N, 3, H, W] RGB tensor
        """
        Y = img_ycbcr[:, 0, :, :]
        Cb = img_ycbcr[:, 1, :, :]
        Cr = img_ycbcr[:, 2, :, :]

        # R = Y + 1.402 * (Cr - 0.5)
        R = Y + 1.402 * (Cr - 0.5)

        # G = Y - 0.344136 * (Cb - 0.5) - 0.714136 * (Cr - 0.5)
        G = Y - 0.344136 * (Cb - 0.5) - 0.714136 * (Cr - 0.5)

        # B = Y + 1.772 * (Cb - 0.5)
        B = Y + 1.772 * (Cb - 0.5)

        return torch.stack([R, G, B], dim=1)

    @staticmethod
    def tensor_to_image(tensor: torch.Tensor) -> np.ndarray:
        """Convert output tensor to OpenCV image

        Args:
            tensor: [N, 3, H, W] or [3, H, W] tensor in [0, 1]

        Returns:
            OpenCV BGR image [H, W, 3] uint8
        """
        # Handle batch
        if len(tensor.shape) == 4:
            tensor = tensor.squeeze(0)

        # Ensure 3 dimensions [C, H, W]
        if len(tensor.shape) != 3 or tensor.shape[0] != 3:
            raise ValueError(f"Expected tensor with shape [3, H, W], got {tensor.shape}")

        # [C, H, W] -> [H, W, C]
        img = tensor.permute(1, 2, 0).cpu().numpy()

        # [0, 1] -> [0, 255]
        img = (img * 255).clip(0, 255).astype(np.uint8)

        # RGB to BGR
        img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)

        return img

    def get_stats(self) -> dict:
        """Get inference statistics"""
        avg_time = self.total_inference_time / max(1, self.inference_count)
        return {
            'inference_count': self.inference_count,
            'total_time': self.total_inference_time,
            'avg_time': avg_time
        }

    def reset_stats(self):
        """Reset inference statistics"""
        self.inference_count = 0
        self.total_inference_time = 0.0


if __name__ == '__main__':
    # Test the engine
    engine = HDREngine(
        checkpoint_dir='/home/jym/python/hdr-net-start/checkpoints',
        gpu_id=0
    )
    engine.warmup()

    # Test single inference
    import requests

    # Download a test image
    test_url = 'http://127.0.0.1:8080/dataset/Select_Test/009/0.jpg'

    # Read local test image instead
    import os
    test_img_path = '/home/jym/python/hdr-net-start/dataset/Select_Test/009/0.jpg'
    if os.path.exists(test_img_path):
        with open(test_img_path, 'rb') as f:
            img_bytes = f.read()

        tensor = engine.preprocess(img_bytes)
        tensor = tensor.unsqueeze(0)  # Add batch dim

        output = engine.infer(tensor, tensor)

        print(f"Input shape: {tensor.shape}")
        print(f"Output shape: {output.shape}")

        img = engine.tensor_to_image(output)
        cv2.imwrite('/tmp/hdr_engine_test.jpg', img)
        print("Saved to /tmp/hdr_engine_test.jpg")

        print(f"Stats: {engine.get_stats()}")
