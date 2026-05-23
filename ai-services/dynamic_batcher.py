#!/usr/bin/env python3
# Dynamic Batcher for HDR Inference
# Collects requests into batches for GPU efficiency

import queue
import threading
import time
import uuid
import torch
import logging
from typing import Optional, Tuple, List

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class BatchRequest:
    """Represents a single inference request in the batch"""

    def __init__(self, request_id: str, img1_bytes: bytes, img2_bytes: bytes,
                 model_type: str):
        self.request_id = request_id
        self.img1_bytes = img1_bytes
        self.img2_bytes = img2_bytes
        self.model_type = model_type
        self.event = threading.Event()
        self.result: Optional[torch.Tensor] = None
        self.exception: Optional[Exception] = None
        self.inference_time_ms: float = 0


class DynamicBatcher:
    """Dynamic Batcher for GPU Inference

    Collects incoming requests into batches for efficient GPU utilization.
    Uses a time window to balance latency and throughput.

    Usage:
        batcher = DynamicBatcher(engine, max_batch_size=8, max_wait_ms=10)
        batcher.start()

        # Add request
        request_id, event = batcher.add_request(img1_bytes, img2_bytes, 'hdr_fusion')
        event.wait()  # Wait for result

        batcher.stop()
    """

    def __init__(self, engine, max_batch_size: int = 8, max_wait_ms: int = 10):
        self.engine = engine
        self.max_batch_size = max_batch_size
        self.max_wait_ms = max_wait_ms

        # Request queues
        self.pending_queue = queue.Queue()
        self.active_requests = {}  # request_id -> BatchRequest

        # Lock for thread-safe access
        self.lock = threading.Lock()

        # Worker thread
        self.running = False
        self.batch_thread: Optional[threading.Thread] = None

        # Stats
        self.total_batches = 0
        self.total_requests = 0

    def start(self):
        """Start the batch processing loop"""
        if self.running:
            logger.warning("Batcher already running")
            return

        self.running = True
        self.batch_thread = threading.Thread(target=self._batch_loop, daemon=True)
        self.batch_thread.start()
        logger.info(f"Batcher started (max_batch={self.max_batch_size}, max_wait={self.max_wait_ms}ms)")

    def stop(self):
        """Stop the batch processing loop"""
        if not self.running:
            return

        self.running = False

        if self.batch_thread:
            self.batch_thread.join(timeout=5)

        logger.info("Batcher stopped")

    def add_request(self, img1_bytes: bytes, img2_bytes: bytes,
                   model_type: str = 'hdr_fusion') -> Tuple[str, threading.Event]:
        """Add a new inference request

        Args:
            img1_bytes: Image 1 bytes (low exposure)
            img2_bytes: Image 2 bytes (high exposure)
            model_type: Model type ('hdr_fusion', 'yolo', etc.)

        Returns:
            Tuple of (request_id, event)
            The event will be set when the result is ready
        """
        request_id = str(uuid.uuid4())
        request = BatchRequest(request_id, img1_bytes, img2_bytes, model_type)

        with self.lock:
            self.active_requests[request_id] = request

        self.pending_queue.put(request)
        self.total_requests += 1

        return request_id, request.event

    def get_result(self, request_id: str, timeout: float = 30) -> Optional[torch.Tensor]:
        """Get the result of a completed request

        Args:
            request_id: The request ID returned from add_request
            timeout: Maximum time to wait in seconds

        Returns:
            The inference result tensor, or None if failed
        """
        with self.lock:
            request = self.active_requests.get(request_id)

        if request is None:
            return None

        # Wait for result
        if not request.event.wait(timeout=timeout):
            logger.warning(f"Request {request_id} timeout")
            return None

        if request.exception:
            raise request.exception

        return request.result

    def _batch_loop(self):
        """Main batch processing loop"""
        while self.running:
            try:
                batch = self._collect_batch()

                if not batch:
                    continue

                self._execute_batch(batch)

            except Exception as e:
                logger.error(f"Batch loop error: {e}")
                time.sleep(0.1)

    def _collect_batch(self) -> List[BatchRequest]:
        """Collect requests into a batch

        Returns when:
        - Batch size reaches max_batch_size, OR
        - Wait timeout exceeds max_wait_ms
        """
        batch = []
        start_time = time.time()

        while len(batch) < self.max_batch_size:
            elapsed_ms = (time.time() - start_time) * 1000

            # If we've waited long enough and have requests, process them
            if elapsed_ms >= self.max_wait_ms and batch:
                break

            # Try to get a request (non-blocking with timeout)
            try:
                request = self.pending_queue.get(timeout=0.001)
                batch.append(request)
            except queue.Empty:
                # No more requests available
                if batch:
                    break
                continue

        return batch

    def _execute_batch(self, batch: List[BatchRequest]):
        """Execute inference for a batch of requests

        Args:
            batch: List of BatchRequest objects
        """
        if not batch:
            return

        self.total_batches += 1
        batch_start = time.time()

        try:
            # Preprocess all images
            img1_tensors = []
            img2_tensors = []

            for req in batch:
                try:
                    t1 = self.engine.preprocess(req.img1_bytes)
                    t2 = self.engine.preprocess(req.img2_bytes)
                    img1_tensors.append(t1)
                    img2_tensors.append(t2)
                except Exception as e:
                    req.exception = e
                    req.event.set()
                    continue

            if not img1_tensors:
                return

            # Stack into batches [N, C, H, W]
            # Each tensor is [1, C, H, W], we need to cat along batch dimension
            # But tensors may have different H, W - resize to same size
            target_h = img1_tensors[0].shape[2]
            target_w = img1_tensors[0].shape[3]

            resized_img1 = []
            resized_img2 = []

            for t1, t2 in zip(img1_tensors, img2_tensors):
                if t1.shape[2:] != (target_h, target_w):
                    t1 = torch.nn.functional.interpolate(t1, size=(target_h, target_w), mode='bilinear')
                    t2 = torch.nn.functional.interpolate(t2, size=(target_h, target_w), mode='bilinear')
                resized_img1.append(t1)
                resized_img2.append(t2)

            batch_img1 = torch.cat(resized_img1, dim=0)  # [N, C, H, W]
            batch_img2 = torch.cat(resized_img2, dim=0)

            # Run inference
            outputs = self.engine.infer(batch_img1, batch_img2)  # [N, 3, H, W]

            # Dispatch results
            for i, req in enumerate(batch):
                if req.exception is None:
                    req.result = outputs[i]
                    req.inference_time_ms = (time.time() - batch_start) * 1000 / len(batch)
                    req.event.set()

        except Exception as e:
            logger.error(f"Batch execution error: {e}")
            for req in batch:
                if req.exception is None:
                    req.exception = e
                    req.event.set()

        finally:
            # Cleanup active requests
            with self.lock:
                for req in batch:
                    self.active_requests.pop(req.request_id, None)

        batch_time = (time.time() - batch_start) * 1000
        logger.debug(f"Batch {self.total_batches}: {len(batch)} requests in {batch_time:.1f}ms")

    def get_stats(self) -> dict:
        """Get batcher statistics"""
        return {
            'total_batches': self.total_batches,
            'total_requests': self.total_requests,
            'active_requests': len(self.active_requests),
            'pending_queue_size': self.pending_queue.qsize(),
            'avg_batch_size': self.total_requests / max(1, self.total_batches)
        }


if __name__ == '__main__':
    # Test the batcher
    from hdr_engine import HDREngine

    print("Loading HDR engine...")
    engine = HDREngine(
        checkpoint_dir='/home/jym/python/hdr-net-start/checkpoints',
        gpu_id=0
    )
    engine.warmup(warmup_iterations=2)

    print("Starting batcher...")
    batcher = DynamicBatcher(engine, max_batch_size=4, max_wait_ms=20)
    batcher.start()

    # Load test images
    test_img_path = '/home/jym/python/hdr-net-start/dataset/Select_Test/009/0.jpg'
    test_img_path2 = '/home/jym/python/hdr-net-start/dataset/Select_Test/009/2.jpg'

    with open(test_img_path, 'rb') as f:
        img1_bytes = f.read()
    with open(test_img_path2, 'rb') as f:
        img2_bytes = f.read()

    # Add 4 requests concurrently
    print("Adding 4 requests...")
    request_ids = []
    events = []

    for i in range(4):
        req_id, event = batcher.add_request(img1_bytes, img2_bytes, 'hdr_fusion')
        request_ids.append(req_id)
        events.append(event)
        print(f"  Request {i}: {req_id}")

    # Wait for all results
    print("Waiting for results...")
    start = time.time()

    for i, (req_id, event) in enumerate(zip(request_ids, events)):
        event.wait(timeout=30)
        result = batcher.get_result(req_id)
        if result is not None:
            print(f"  Request {i}: SUCCESS, shape={result.shape}")
        else:
            print(f"  Request {i}: FAILED")

    elapsed = (time.time() - start) * 1000
    print(f"\nTotal time for 4 requests: {elapsed:.1f}ms")
    print(f"Stats: {batcher.get_stats()}")

    # Save one result
    result = batcher.get_result(request_ids[0])
    if result is not None:
        import cv2
        import numpy as np
        img = HDREngine.tensor_to_image(result)
        cv2.imwrite('/tmp/batcher_test_output.jpg', img)
        print("Saved output to /tmp/batcher_test_output.jpg")

    batcher.stop()
