#!/usr/bin/env python3
# GPU Memory Manager
# Handles memory cleanup and fragmentation prevention

import torch
import threading
import time
import logging
import gc

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class GPUMemoryManager:
    """GPU Memory Health Manager

    Monitors and maintains GPU memory health:
    - Periodic cleanup to prevent fragmentation
    - Memory usage monitoring
    - Automatic cleanup when threshold is exceeded

    Usage:
        manager = GPUMemoryManager(gpu_id=0)
        manager.start()

        # In inference loop:
        manager.record_inference()

        manager.stop()
    """

    def __init__(self, gpu_id: int = 0,
                 cleanup_interval: int = 100,
                 warning_threshold: float = 0.85,
                 cleanup_threshold: float = 0.90):
        self.gpu_id = gpu_id
        self.cleanup_interval = cleanup_interval  # Cleanup every N inferences
        self.warning_threshold = warning_threshold  # 85%
        self.cleanup_threshold = cleanup_threshold  # 90%

        self.inference_count = 0
        self.last_cleanup_time = time.time()

        # Monitoring thread
        self.running = False
        self.monitor_thread: threading.Thread = None

        # Stats
        self.cleanup_count = 0
        self.total_cleanup_time = 0.0

    def record_inference(self):
        """Call this after each inference to track memory usage"""
        self.inference_count += 1

        # Periodic cleanup
        if self.inference_count >= self.cleanup_interval:
            self._cleanup()

    def should_cleanup(self) -> bool:
        """Check if memory cleanup is needed"""
        if not torch.cuda.is_available():
            return False

        allocated = torch.cuda.memory_allocated(self.gpu_id)
        total = torch.cuda.get_device_properties(self.gpu_id).total_memory
        usage = allocated / total

        return usage > self.cleanup_threshold

    def get_memory_usage(self) -> dict:
        """Get current GPU memory usage statistics"""
        if not torch.cuda.is_available():
            return {'available': False}

        allocated = torch.cuda.memory_allocated(self.gpu_id)
        reserved = torch.cuda.memory_reserved(self.gpu_id)
        total = torch.cuda.get_device_properties(self.gpu_id).total_memory
        max_allocated = torch.cuda.max_memory_allocated(self.gpu_id)

        return {
            'available': True,
            'gpu_id': self.gpu_id,
            'allocated_mb': allocated / (1024 ** 2),
            'reserved_mb': reserved / (1024 ** 2),
            'total_mb': total / (1024 ** 2),
            'usage_percent': (allocated / total) * 100,
            'max_allocated_mb': max_allocated / (1024 ** 2),
            'inference_count': self.inference_count,
            'cleanup_count': self.cleanup_count
        }

    def _cleanup(self):
        """Perform GPU memory cleanup"""
        if not torch.cuda.is_available():
            return

        start_time = time.time()
        logger.info(f"[GPU {self.gpu_id}] Starting memory cleanup...")

        # 1. Run Python garbage collection
        gc.collect()

        # 2. Clear PyTorch CUDA cache
        torch.cuda.empty_cache()

        # 3. Optional: Reset accumulated memory stats
        # torch.cuda.reset_accumulated_memory_stats()
        # torch.cuda.reset_peak_memory_stats()

        elapsed = time.time() - start_time
        self.cleanup_count += 1
        self.total_cleanup_time += elapsed
        self.inference_count = 0

        # Log memory stats after cleanup
        stats = self.get_memory_usage()
        logger.info(
            f"[GPU {self.gpu_id}] Memory cleanup complete: "
            f"allocated={stats['allocated_mb']:.1f}MB, "
            f"reserved={stats['reserved_mb']:.1f}MB, "
            f"usage={stats['usage_percent']:.1f}%, "
            f"cleanup_time={elapsed*1000:.1f}ms"
        )

    def start_monitoring(self, interval_seconds: int = 60):
        """Start background monitoring thread

        Args:
            interval_seconds: How often to check memory (seconds)
        """
        if self.running:
            logger.warning("Monitor already running")
            return

        self.running = True
        self.monitor_thread = threading.Thread(
            target=self._monitor_loop,
            args=(interval_seconds,),
            daemon=True
        )
        self.monitor_thread.start()
        logger.info(f"[GPU {self.gpu_id}] Memory monitor started (interval={interval_seconds}s)")

    def stop_monitoring(self):
        """Stop background monitoring"""
        if not self.running:
            return

        self.running = False

        if self.monitor_thread:
            self.monitor_thread.join(timeout=5)

        logger.info("[GPU {self.gpu_id}] Memory monitor stopped")

    def _monitor_loop(self, interval_seconds: int):
        """Background monitoring loop"""
        while self.running:
            try:
                stats = self.get_memory_usage()

                if stats['available']:
                    usage = stats['usage_percent']

                    # Warning level
                    if usage > self.warning_threshold * 100:
                        logger.warning(
                            f"[GPU {self.gpu_id}] High memory usage: {usage:.1f}% "
                            f"(threshold: {self.warning_threshold*100:.1f}%)"
                        )
                        # Trigger cleanup
                        if self.should_cleanup():
                            self._cleanup()

            except Exception as e:
                logger.error(f"[GPU {self.gpu_id}] Monitor error: {e}")

            time.sleep(interval_seconds)

    def reset_stats(self):
        """Reset statistics"""
        self.inference_count = 0
        self.cleanup_count = 0
        self.total_cleanup_time = 0.0

    def get_stats(self) -> dict:
        """Get memory manager statistics"""
        return {
            'cleanup_count': self.cleanup_count,
            'total_cleanup_time': self.total_cleanup_time,
            'avg_cleanup_time': self.total_cleanup_time / max(1, self.cleanup_count),
            'inference_count': self.inference_count
        }


class MemoryGuard:
    """Context manager for GPU memory safety

    Usage:
        with MemoryGuard(engine):
            result = engine.infer(img1, img2)
    """

    def __init__(self, engine, manager: GPUMemoryManager = None):
        self.engine = engine
        self.manager = manager or GPUMemoryManager(gpu_id=engine.gpu_id)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # Record inference
        self.manager.record_inference()

        # Check if cleanup needed
        if self.manager.should_cleanup():
            self.manager._cleanup()

        return False


if __name__ == '__main__':
    # Test the memory manager
    manager = GPUMemoryManager(gpu_id=0)
    manager.start_monitoring(interval_seconds=10)

    # Get initial stats
    stats = manager.get_memory_usage()
    print(f"Initial stats: {stats}")

    # Simulate some inferences
    for i in range(150):
        manager.record_inference()

    # Get final stats
    stats = manager.get_memory_usage()
    print(f"After 150 inferences: {stats}")

    manager.stop_monitoring()

    print(f"Manager stats: {manager.get_stats()}")
