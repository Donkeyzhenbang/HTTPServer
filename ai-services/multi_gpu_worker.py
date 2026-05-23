#!/usr/bin/env python3
# Multi-GPU HDR Inference Worker
# Supports dynamic batching and GPU memory management

import argparse
import json
import base64
import cv2
import numpy as np
import torch
import zmq
import logging
import signal
import sys
import time
import threading
from typing import Optional

from hdr_engine import HDREngine
from dynamic_batcher import DynamicBatcher
from memory_manager import GPUMemoryManager

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class MultiGPUWorker:
    """Multi-GPU HDR Inference Worker

    Features:
    - Dynamic batching for GPU efficiency
    - GPU memory management
    - ZMQ REP interface for inference requests

    Usage:
        worker = MultiGPUWorker(port=50055, gpu_id=0, model='hdr_fusion')
        worker.start()
    """

    def __init__(self, port: int = 50055, gpu_id: int = 0,
                 model_type: str = 'hdr_fusion',
                 checkpoint_dir: str = '/home/jym/python/hdr-net-start/checkpoints',
                 max_batch_size: int = 8,
                 max_wait_ms: int = 10):
        self.port = port
        self.gpu_id = gpu_id
        self.model_type = model_type
        self.checkpoint_dir = checkpoint_dir
        self.max_batch_size = max_batch_size
        self.max_wait_ms = max_wait_ms

        # Initialize components
        self.hdr_engine: Optional[HDREngine] = None
        self.batcher: Optional[DynamicBatcher] = None
        self.memory_manager: Optional[GPUMemoryManager] = None

        # ZMQ
        self.ctx: Optional[zmq.Context] = None
        self.socket: Optional[zmq.Socket] = None

        # State
        self.running = False
        self.start_time = time.time()

    def initialize(self):
        """Initialize all components"""
        logger.info(f"[GPU {self.gpu_id}] Initializing worker...")

        # Initialize HDR Engine
        logger.info(f"[GPU {self.gpu_id}] Loading HDR engine...")
        self.hdr_engine = HDREngine(
            checkpoint_dir=self.checkpoint_dir,
            gpu_id=self.gpu_id
        )
        self.hdr_engine.warmup(warmup_iterations=2)
        logger.info(f"[GPU {self.gpu_id}] HDR engine ready")

        # Initialize Dynamic Batcher
        self.batcher = DynamicBatcher(
            engine=self.hdr_engine,
            max_batch_size=self.max_batch_size,
            max_wait_ms=self.max_wait_ms
        )
        self.batcher.start()
        logger.info(f"[GPU {self.gpu_id}] Dynamic batcher started")

        # Initialize Memory Manager
        self.memory_manager = GPUMemoryManager(
            gpu_id=self.gpu_id,
            cleanup_interval=100,
            warning_threshold=0.85,
            cleanup_threshold=0.90
        )
        self.memory_manager.start_monitoring(interval_seconds=60)
        logger.info(f"[GPU {self.gpu_id}] Memory manager started")

        # Initialize ZMQ
        self.ctx = zmq.Context()
        self.socket = self.ctx.socket(zmq.REP)
        self.socket.setsockopt(zmq.RCVTIMEO, 30000)  # 30s timeout
        self.socket.setsockopt(zmq.SNDTIMEO, 30000)
        self.socket.bind(f"tcp://*:{self.port}")
        logger.info(f"[GPU {self.gpu_id}] ZMQ socket bound to port {self.port}")

    def start(self):
        """Start the worker main loop"""
        if self.running:
            logger.warning("Worker already running")
            return

        self.initialize()
        self.running = True

        logger.info(f"[GPU {self.gpu_id}] Worker started successfully")
        logger.info(f"  Port: {self.port}")
        logger.info(f"  GPU: {self.gpu_id}")
        logger.info(f"  Model: {self.model_type}")
        logger.info(f"  Batch size: {self.max_batch_size}")
        logger.info(f"  Max wait: {self.max_wait_ms}ms")

        # Register signal handlers
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

        # Main loop
        try:
            while self.running:
                try:
                    self._process_request()
                except zmq.Again:
                    # Timeout, continue loop
                    continue
                except Exception as e:
                    logger.error(f"Error processing request: {e}")
                    try:
                        self.socket.send_json({'error': str(e)})
                    except:
                        pass
        finally:
            self.shutdown()

    def stop(self):
        """Stop the worker"""
        self.running = False

    def shutdown(self):
        """Shutdown all components"""
        logger.info(f"[GPU {self.gpu_id}] Shutting down worker...")

        self.running = False

        # Stop batcher
        if self.batcher:
            self.batcher.stop()

        # Stop memory monitor
        if self.memory_manager:
            self.memory_manager.stop_monitoring()

        # Close ZMQ
        if self.socket:
            try:
                self.socket.close()
            except:
                pass

        if self.ctx:
            try:
                self.ctx.term()
            except:
                pass

        # Log final stats
        uptime = time.time() - self.start_time
        logger.info(f"[GPU {self.gpu_id}] Worker uptime: {uptime:.1f}s")

        if self.batcher:
            stats = self.batcher.get_stats()
            logger.info(f"[GPU {self.gpu_id}] Batcher stats: {stats}")

        if self.memory_manager:
            mm_stats = self.memory_manager.get_stats()
            logger.info(f"[GPU {self.gpu_id}] Memory stats: {mm_stats}")

        logger.info(f"[GPU {self.gpu_id}] Worker shutdown complete")

    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        logger.info(f"[GPU {self.gpu_id}] Received signal {signum}, shutting down...")
        self.stop()

    def _process_request(self):
        """Process a single inference request"""
        # Receive multipart message
        message = self.socket.recv_multipart()

        if len(message) < 2:
            self.socket.send_json({'error': 'Invalid message format'})
            return

        # Parse metadata
        try:
            meta = json.loads(message[0].decode('utf-8'))
        except:
            self.socket.send_json({'error': 'Invalid JSON metadata'})
            return

        model_type = meta.get('model', self.model_type)

        # Get images
        img1_bytes = message[1]
        img2_bytes = img1_bytes  # Default to same image

        if len(message) > 2 and len(message[2]) > 0:
            img2_bytes = message[2]

        start_time = time.time()

        # Add to batcher
        request_id, event = self.batcher.add_request(
            img1_bytes, img2_bytes, model_type
        )

        # Wait for result
        event.wait(timeout=30)

        try:
            result = self.batcher.get_result(request_id, timeout=1)

            if result is None:
                response = {'error': 'Inference timeout'}
            else:
                # Convert tensor to image
                output_img = HDREngine.tensor_to_image(result)

                # Encode to JPEG
                _, buffer = cv2.imencode('.jpg', output_img,
                                        [cv2.IMWRITE_JPEG_QUALITY, 95])
                encoded = base64.b64encode(buffer).decode('utf-8')

                inference_time = (time.time() - start_time) * 1000

                response = {
                    'status': 'success',
                    'model_used': f'{model_type}_batched',
                    'inference_time_ms': int(inference_time),
                    'batch_size': 1,  # TODO: track actual batch size
                    'image_b64': f'data:image/jpeg;base64,{encoded}'
                }

                # Record inference for memory management
                self.memory_manager.record_inference()

        except Exception as e:
            logger.error(f"Inference error: {e}")
            response = {'error': str(e)}

        # Send response
        self.socket.send_json(response)


def main():
    parser = argparse.ArgumentParser(description='Multi-GPU HDR Worker')
    parser.add_argument('--port', type=int, default=50055,
                       help='ZMQ REP port (default: 50055)')
    parser.add_argument('--gpu-id', type=int, default=0,
                       help='GPU ID to use (default: 0)')
    parser.add_argument('--model', type=str, default='hdr_fusion',
                       choices=['hdr_fusion', 'yolo', 'segmentation', 'all'],
                       help='Model type (default: hdr_fusion)')
    parser.add_argument('--checkpoint-dir', type=str,
                       default='/home/jym/python/hdr-net-start/checkpoints',
                       help='Model checkpoint directory')
    parser.add_argument('--max-batch-size', type=int, default=8,
                       help='Maximum batch size (default: 8)')
    parser.add_argument('--max-wait-ms', type=int, default=10,
                       help='Maximum wait time in ms (default: 10)')
    parser.add_argument('--verbose', action='store_true',
                       help='Enable verbose logging')

    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    worker = MultiGPUWorker(
        port=args.port,
        gpu_id=args.gpu_id,
        model_type=args.model,
        checkpoint_dir=args.checkpoint_dir,
        max_batch_size=args.max_batch_size,
        max_wait_ms=args.max_wait_ms
    )

    try:
        worker.start()
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
        worker.stop()


if __name__ == '__main__':
    main()
