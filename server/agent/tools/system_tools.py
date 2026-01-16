import psutil
import socket
from datetime import datetime

class SystemMonitorTools:
    """系统状态监控工具集"""
    
    def get_system_status(self, query: str = "") -> str:
        """获取服务器当前的系统资源状态。当用户问‘系统状态’、‘服务器负载’时使用。"""
        try:
            # CPU
            cpu_percent = psutil.cpu_percent(interval=0.5)
            cpu_count = psutil.cpu_count()
            
            # 内存
            memory = psutil.virtual_memory()
            memory_used_gb = memory.used / (1024**3)
            memory_total_gb = memory.total / (1024**3)
            memory_percent = memory.percent
            
            # 磁盘 (根路径)
            disk = psutil.disk_usage('/')
            disk_used_gb = disk.used / (1024**3)
            disk_total_gb = disk.total / (1024**3)
            disk_percent = disk.percent
            
            # 网络 (简要信息)
            hostname = socket.gethostname()
            try:
                ip_address = socket.gethostbyname(hostname)
            except:
                ip_address = "127.0.0.1"
            
            # 格式化输出
            status_report = (
                "【系统状态报告】\n"
                f"• 主机: {hostname} ({ip_address})\n"
                f"• CPU: 使用率 {cpu_percent}% ({cpu_count} 核心)\n"
                f"• 内存: 使用率 {memory_percent}% ({memory_used_gb:.1f}GB / {memory_total_gb:.1f}GB)\n"
                f"• 磁盘(根): 使用率 {disk_percent}% ({disk_used_gb:.1f}GB / {disk_total_gb:.1f}GB)\n"
                f"• 当前时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
            )
            return status_report
        except Exception as e:
            return f"获取系统状态时出错: {str(e)}"
    
    def get_detailed_disk_info(self, query: str = "") -> str:
        """获取所有磁盘分区的详细信息。"""
        try:
            disk_info = []
            for part in psutil.disk_partitions(all=False):
                if part.fstype:
                    try:
                        usage = psutil.disk_usage(part.mountpoint)
                        disk_info.append(
                            f"{part.device} ({part.mountpoint}): "
                            f"{usage.percent}% 已用, "
                            f"{usage.used/(1024**3):.1f}GB / {usage.total/(1024**3):.1f}GB"
                        )
                    except:
                        continue
            if disk_info:
                return "磁盘分区详情：\n" + "\n".join(f"• {info}" for info in disk_info)
            else:
                return "无法获取磁盘分区信息。"
        except Exception as e:
            return f"获取磁盘信息时出错: {str(e)}"