/************************************************************************
 * 要点：
 * - 缩略图按组显示：每组 5 张（可通过 --thumb-group-size 修改）。
 * - 当主图索引跳转到不在当前缩略图组（向左或向右）时，会自动切换到包含该图片的组。
 * - 不修改后端接口或后端逻辑（保留 /api/... 与 /uploads/... 的调用）。
 ************************************************************************/

let imagesList = [];
let currentIndex = -1;
let scale = 1;
let translate = {x:0,y:0};
let isPanning = false;
let panStart = {x:0,y:0};
let refreshInterval;
let currentChannel = 1; // 当前选中的通道
let channels = [1, 2, 3, 4, 5, 6, 7]; // 支持6个通道
let currentDate = new Date(); // 当前选中的日期
let filteredImagesByChannel = {}; // 按通道过滤的图片

// 缩略图组管理
const THUMB_GROUP_SIZE = 5;
let thumbGroupStart = 0; // 当前组起始索引（在 imagesList 中）

async function fetchJSON(url, opts = {}) {
  const r = await fetch(url, opts);
  return r.json();
}

/* 设备 */
async function loadDevices() {
  try {
    const data = await fetchJSON('/api/devices');
    const c = document.getElementById('devicesContainer');
    c.innerHTML = '';

    if (!data.devices || data.devices.length === 0) {
      const emptyMsg = document.createElement('div');
      emptyMsg.className = 'device';
      emptyMsg.innerHTML = `
        <div style="text-align:center;width:100%;color:var(--muted)">
          <div>当前无已注册设备</div>
          <div style="font-size:12px;color:#5d8c5f;margin-top:6px">总连接数: ${data.connections||0}</div>
        </div>
      `;
      c.appendChild(emptyMsg);
      
      // 清空升级弹窗的设备列表
      const targetDeviceSelect = document.getElementById('targetDevice');
      if (targetDeviceSelect) targetDeviceSelect.innerHTML = '<option value="">请选择设备...</option>';
      return;
    }

    data.devices.forEach(dev => {
      const div = document.createElement('div');
      div.className = 'device';
      div.innerHTML = `
        <div>
          <strong>${dev}</strong>
          <div class="meta">设备ID: ${dev}</div>
        </div>
        <div style="display:flex;flex-direction:column;gap:8px;align-items:flex-end">
          <span style="background:linear-gradient(180deg,var(--accent),var(--accent-2));color:#ffffff;padding:6px 10px;border-radius:999px;font-weight:700;font-size:12px">已注册</span>
          <button class="btn small" style="margin-top:6px;background:linear-gradient(90deg,var(--accent),var(--accent-2));color:#ffffff;border:none;padding:6px 10px;border-radius:6px;">📷 要图</button>
        </div>
      `;
      
      // 添加要图按钮事件
      const btn = div.querySelector('button');
      btn.onclick = async () => {
        btn.disabled = true;
        const old = btn.innerHTML;
        btn.innerText = '发送中…';
        try {
          const resp = await fetch('/api/send_b341', {
            method:'POST',
            headers:{'Content-Type':'application/json'},
            body: JSON.stringify({ device: dev, channel: currentChannel })
          });
          const j = await resp.json();
          if (j.ok) {
            showNotification(`已向设备 ${dev} 发送要图指令`, 'success');
            // 等待3秒后刷新图片
            setTimeout(async () => {
              await loadImages();
            }, 3000);
          } else {
            showNotification(`发送失败: ${j.error || '未知'}`, 'error');
          }
        } catch(e) {
          showNotification('请求失败: '+ e.message,'error');
        } finally {
          btn.disabled = false;
          btn.innerHTML = old;
        }
      };
      
      c.appendChild(div);
    });

    // 填充升级弹窗的设备列表
    const targetDeviceSelect = document.getElementById('targetDevice');
    if (targetDeviceSelect) {
      targetDeviceSelect.innerHTML = '<option value="">请选择设备...</option>';
      data.devices.forEach(dev => {
        const option = document.createElement('option');
        option.value = dev;
        option.textContent = dev;
        targetDeviceSelect.appendChild(option);
      });
    }

  } catch (e) {
    console.error('加载设备失败', e);
    const c = document.getElementById('devicesContainer');
    if (c) c.innerHTML = '<div class="device" style="color:#f44336">加载设备列表失败，请检查服务器</div>';
  }
}

/* 图片加载与分组机制 */
async function loadImages() {
  try {
    // 添加通道参数到请求
    const arr = await fetchJSON('/api/images?channel=' + currentChannel);
    imagesList = Array.isArray(arr) ? arr : [];
    
    // 如果有新图片，选择最后一张
    if (imagesList.length > 0) {
      setCurrentIndex(imagesList.length - 1, {autoAdjustGroup:true});
      renderThumbGroup();
    } else {
      renderViewerEmpty();
      renderThumbGroup();
    }
  } catch (e) {
    console.error('加载图片失败', e);
    renderViewerEmpty();
    renderThumbGroup();
  }
}

// 按通道过滤图片 (Deprecated/Unused as API handles filtering)
function filterImagesByChannel() {
  filteredImagesByChannel = {};
  channels.forEach(channel => {
    filteredImagesByChannel[channel] = imagesList.filter(img => {
      return img.includes(`ch${channel}`) || img.includes(`CH${channel}`) || 
             img.includes(`通道${channel}`) || img.includes(`channel${channel}`);
    });
  });
}

// 初始化日历
function initCalendar() {
  const container = document.getElementById('datePickerContainer');
  if (container) renderCalendar(currentDate, container);
}

// 渲染日历
function renderCalendar(date, container) {
  const year = date.getFullYear();
  const month = date.getMonth();
  
  // 月份名称
  const monthNames = ["一月", "二月", "三月", "四月", "五月", "六月",
                     "七月", "八月", "九月", "十月", "十一月", "十二月"];
  
  // 星期名称
  const dayNames = ["日", "一", "二", "三", "四", "五", "六"];
  
  // 获取当月第一天是星期几
  const firstDay = new Date(year, month, 1).getDay();
  
  // 获取当月天数
  const daysInMonth = new Date(year, month + 1, 0).getDate();
  
  // 获取上个月天数
  const prevMonthDays = new Date(year, month, 0).getDate();
  
  let html = `
    <div class="date-header">
      <button class="prev-month">‹</button>
      <span style="font-weight:700;color:var(--accent)">${year}年 ${monthNames[month]}</span>
      <button class="next-month">›</button>
    </div>
    <div class="calendar-grid">
  `;
  
  // 星期标题
  for (let i = 0; i < 7; i++) {
    html += `<div class="calendar-day-header">${dayNames[i]}</div>`;
  }
  
  // 上个月的日期
  for (let i = firstDay - 1; i >= 0; i--) {
    const day = prevMonthDays - i;
    html += `<div class="calendar-day other-month">${day}</div>`;
  }
  
  // 当前月的日期
  const today = new Date();
  const isToday = (day) => {
    return year === today.getFullYear() && 
           month === today.getMonth() && 
           day === today.getDate();
  };
  
  const isSelected = (day) => {
    return year === currentDate.getFullYear() && 
           month === currentDate.getMonth() && 
           day === currentDate.getDate();
  };
  
  for (let day = 1; day <= daysInMonth; day++) {
    const todayClass = isToday(day) ? 'today' : '';
    const selectedClass = isSelected(day) ? 'selected' : '';
    html += `<div class="calendar-day ${todayClass} ${selectedClass}" data-day="${day}">${day}</div>`;
  }
  
  // 下个月的日期（补齐网格）
  const totalCells = 42; // 6行 * 7列
  const cellsUsed = firstDay + daysInMonth;
  const nextMonthDays = totalCells - cellsUsed;
  
  for (let day = 1; day <= nextMonthDays; day++) {
    html += `<div class="calendar-day other-month">${day}</div>`;
  }
  
  html += `</div>`;
  container.innerHTML = html;
  
  // 添加事件监听
  container.querySelector('.prev-month').addEventListener('click', () => {
    currentDate = new Date(year, month - 1, 1);
    renderCalendar(currentDate, container);
  });
  
  container.querySelector('.next-month').addEventListener('click', () => {
    currentDate = new Date(year, month + 1, 1);
    renderCalendar(currentDate, container);
  });
  
  // 日期点击事件
  container.querySelectorAll('.calendar-day:not(.other-month)').forEach(dayEl => {
    dayEl.addEventListener('click', () => {
      const day = parseInt(dayEl.dataset.day);
      currentDate = new Date(year, month, day);
      renderCalendar(currentDate, container);
      showNotification(`已选择日期: ${year}-${month+1}-${day}`, 'info');
      // 这里可以添加按日期过滤图片的逻辑
    });
  });
}

function initChannels() {
  const channelsList = document.getElementById('channelsList');
  if (!channelsList) return;
  channelsList.innerHTML = '';
  
  channels.forEach(channel => {
    const btn = document.createElement('button');
    btn.className = `channel-btn ${channel === currentChannel ? 'active' : ''}`;
    btn.dataset.channel = channel;
    // 修改显示文本：通道7显示为"默认通道" (Original logic preserved)
    btn.textContent = channel === 7 ? '默认通道' : `通道 ${channel}`;
    btn.addEventListener('click', async () => {
      // 更新当前通道
      currentChannel = channel;
      
      // 更新按钮状态
      document.querySelectorAll('.channel-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      
      // 更新页面显示
      updateChannelDisplay();
      
      // 加载对应通道的图片
      await loadImages();
      
      const channelName = channel === 7 ? '默认通道' : `通道 ${channel}`;
      showNotification(`已切换到${channelName}`, 'info');
    });
    
    channelsList.appendChild(btn);
  });
}

// 升级弹窗相关功能
function initUpgradeModal() {
  const upgradeBtn = document.getElementById('upgradeBtn');
  const upgradeModal = document.getElementById('upgradeModal');
  const modalClose = document.getElementById('modalClose');
  const cancelUpgrade = document.getElementById('cancelUpgrade');
  const startUpgrade = document.getElementById('startUpgrade');
  const modelFileInput = document.getElementById('modelFile');
  const upgradeLog = document.getElementById('upgradeLog');
  
  if (!upgradeBtn || !upgradeModal) return;

  // 打开弹窗
  upgradeBtn.addEventListener('click', () => {
    upgradeModal.classList.add('active');
    addLogEntry('准备上传模型文件...', 'info');
  });
  
  // 关闭弹窗
  function closeModal() {
    upgradeModal.classList.remove('active');
    // 重置表单
    if(modelFileInput) modelFileInput.value = '';
    const deviceSelect = document.getElementById('targetDevice');
    const typeSelect = document.getElementById('modelType');
    if(deviceSelect) deviceSelect.selectedIndex = 0;
    if(typeSelect) ttypeSelect.selectedIndex = 0; // 重置模型类型选择
    if(upgradeLog) upgradeLog.innerHTML = '<div class="log-entry info">等待上传模型文件...</div>';
  }
  
  if(modalClose) modalClose.addEventListener('click', closeModal);
  if(cancelUpgrade) cancelUpgrade.addEventListener('click', closeModal);
  

  // 模型升级
  if(startUpgrade) startUpgrade.addEventListener('click', async () => {
    const file = modelFileInput.files[0];
    const device = document.getElementById('targetDevice').value;
    const modelType = document.getElementById('modelType').value; // 获取模型类型
    
    if (!file) {
      addLogEntry('错误：请选择模型文件', 'error');
      return;
    }
    
    if (!device) {
      addLogEntry('错误：请选择目标设备', 'error');
      return;
    }
    
    if (!modelType) {
      addLogEntry('错误：请选择模型类型', 'error');
      return;
    }
    
    // 获取模型类型对应的中文名称
    const modelTypeNames = {
      '11': '多曝光融合',
      '22': '弱光增强',
      '33': '异物检测',
      '44': 'YOLO检测',
      '55': '去雾'
    };
    const modelTypeName = modelTypeNames[modelType] || '未知类型';
    
    addLogEntry(`开始上传模型文件: ${file.name} (${(file.size/1024/1024).toFixed(2)}MB)`, 'info');
    addLogEntry(`目标设备: ${device}`, 'info');
    addLogEntry(`模型类型: ${modelTypeName}`, 'info');
    addLogEntry('正在准备传输...', 'info');
    
    // 实际传输模型文件
    try {
      const formData = new FormData();
      formData.append('model', file);
      formData.append('device', device);
      formData.append('modelType', modelType); 
      
      addLogEntry('正在上传文件到服务器...', 'info');
      
      const uploadResponse = await fetch('/api/upload_model', {
        method: 'POST',
        body: formData
      });
      
      // 检查响应状态
      if (!uploadResponse.ok) {
        throw new Error(`HTTP ${uploadResponse.status}: ${uploadResponse.statusText}`);
      }
      
      // 解析JSON响应
      const uploadResult = await uploadResponse.json();
      
      if (uploadResult.ok) {
        addLogEntry('模型文件上传成功', 'info');
        // 开始模拟升级过程（包括抓拍测试）
        simulateUpgradeProcess(file, device, upgradeLog);
      } else {
        addLogEntry(`模型文件上传失败: ${uploadResult.error}`, 'error');
      }
    } catch (error) {
      console.error('上传错误详情:', error);
      
      // 提供更详细的错误信息
      if (error.name === 'TypeError' && error.message.includes('JSON')) {
        addLogEntry('上传请求失败: 服务器返回了非JSON响应，可能是服务器错误', 'error');
      } else {
        addLogEntry(`上传请求失败: ${error.message}`, 'error');
      }
    }
  });
    
  // 点击弹窗外部关闭
  upgradeModal.addEventListener('click', (e) => {
    if (e.target === upgradeModal) {
      closeModal();
    }
  });
}

// 添加日志条目
function addLogEntry(message, type = 'info') {
  const logOutput = document.getElementById('upgradeLog');
  if (!logOutput) return;
  const entry = document.createElement('div');
  entry.className = `log-entry ${type}`;
  entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
  logOutput.appendChild(entry);
  logOutput.scrollTop = logOutput.scrollHeight;
}

function simulateUpgradeProcess(file, device, logOutput) {
  // 清空日志
  if (!logOutput) return;
  logOutput.innerHTML = '';
  
  const steps = [
      {delay: 500, message: '正在验证模型文件格式...', type: 'info'},
      {delay: 1000, message: '文件验证通过', type: 'info'},
      {delay: 800, message: `正在连接设备: ${device}...`, type: 'info'},
      {delay: 1200, message: '设备连接成功', type: 'info'},
      {delay: 600, message: '开始传输模型文件...', type: 'info'},
      {delay: 600, message: '模型升级完成！', type: 'info'},
  ];
  
  let totalDelay = 0;
  steps.forEach(step => {
      setTimeout(() => {
          addLogEntry(step.message, step.type);
      }, totalDelay);
      totalDelay += step.delay;
  });
  
  // 完成后显示成功消息
  setTimeout(() => {
      showNotification('模型升级完成！', 'success');
  }, totalDelay + 5000); 
}

// 新增函数：执行模型升级后的抓拍测试
async function performCaptureTestAfterUpgrade(device) {
  try {
      // 使用当前通道进行测试
      const channel = currentChannel;
      
      addLogEntry(`正在向设备 ${device} 发送抓拍测试指令（通道${channel}）...`, 'info');
      
      // 调用后端API进行抓拍测试
      const response = await fetch('/api/test_capture_after_upgrade', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({
              device: device,
              channel: channel
          })
      });
      
      const result = await response.json();
      
      if (result.ok) {
          addLogEntry('抓拍测试成功完成！设备功能正常。', 'info');
          
          // 等待一段时间后刷新图片列表
          setTimeout(async () => {
              addLogEntry('正在检查是否有新抓拍图片...', 'info');
              await loadImages();
              addLogEntry('图片列表已刷新。', 'info');
          }, 2000);
      } else {
          addLogEntry(`抓拍测试失败: ${result.error || '未知错误'}`, 'error');
      }
  } catch (error) {
      addLogEntry(`抓拍测试请求失败: ${error.message}`, 'error');
  }
}

/* 刷新按钮功能增强 */
const refreshBtn = document.getElementById('refreshAll');
if (refreshBtn) {
  refreshBtn.addEventListener('click', async () => {
    try {
      // 清除缓存
      imagesList = [];
      currentIndex = -1;
      thumbGroupStart = 0;
      
      // 显示加载状态
      const viewerInner = document.getElementById('viewerInner');
      if(viewerInner) viewerInner.innerHTML = '<div style="color:var(--muted)">刷新中...</div>';
      
      // 并行刷新设备和当前通道的图片
      await Promise.all([
        loadDevices(),
        loadImages()  // 这里会自动使用当前通道
      ]);
      showNotification(`已刷新通道 ${currentChannel} 的图片和设备列表`, 'success');
    } catch (e) {
      console.error('刷新失败', e);
      showNotification('刷新失败: ' + e.message, 'error');
    }
  });
}

function attachThumbClickEvents() {
  const windowEl = document.getElementById('thumbWindow');
  if(!windowEl) return;
  const items = Array.from(windowEl.querySelectorAll('.thumb-item'));
  
  items.forEach(item => {
    item.onclick = null;
    item.onclick = () => {
      const globalIdx = parseInt(item.dataset.index);
      const img = item.querySelector('img');
      if (img) {
        img.src = '/uploads/' + encodeURIComponent(item.dataset.name);
      }
      setCurrentIndex(globalIdx, {autoAdjustGroup:true, smoothScroll:true});
    };
  });
}

function renderThumbGroup(){
  const windowEl = document.getElementById('thumbWindow');
  if(!windowEl) return;
  windowEl.innerHTML = '';
  if (!imagesList || imagesList.length === 0) return;
  
  if (thumbGroupStart < 0) thumbGroupStart = 0;
  if (thumbGroupStart > Math.max(0, imagesList.length - 1)) thumbGroupStart = Math.max(0, imagesList.length - 1);
  
  const group = imagesList.slice(thumbGroupStart, thumbGroupStart + THUMB_GROUP_SIZE);
  group.forEach((name, localIdx) => {
    const globalIdx = thumbGroupStart + localIdx;
    const item = document.createElement('div');
    item.className = 'thumb-item';
    item.dataset.name = name;
    item.dataset.index = globalIdx;
    
    const img = document.createElement('img');
    img.src = '/uploads/' + encodeURIComponent(name);
    img.alt = name;
    item.appendChild(img);
    
    const cap = document.createElement('div');
    cap.className = 'thumb-caption';
    cap.innerText = name.length > 18 ? name.slice(0,18) + '…' : name;
    item.appendChild(cap);

    windowEl.appendChild(item);
  });
  
  attachThumbClickEvents();
  updateThumbActiveState();
  updateStripArrowState();
  updateThumbWindowTransform();
}

function updateThumbWindowTransform(){
  const windowEl = document.getElementById('thumbWindow');
  if(windowEl) windowEl.style.transform = `translateX(0px)`;
}

function updateThumbActiveState(){
  const windowEl = document.getElementById('thumbWindow');
  if(!windowEl) return;
  const items = Array.from(windowEl.querySelectorAll('.thumb-item'));
  items.forEach(it => it.classList.remove('active'));
  if (currentIndex >= 0) {
    const active = items.find(it => Number(it.dataset.index) === currentIndex);
    if (active) active.classList.add('active');
  }
}

function updateStripArrowState(){
  const left = document.getElementById('stripPrev');
  const right = document.getElementById('stripNext');
  if(left) {
    left.style.opacity = thumbGroupStart > 0 ? '1' : '0.28';
    left.style.pointerEvents = thumbGroupStart > 0 ? 'auto' : 'none';
  }
  if(right) {
    right.style.opacity = (thumbGroupStart + THUMB_GROUP_SIZE) < imagesList.length ? '1' : '0.28';
    right.style.pointerEvents = (thumbGroupStart + THUMB_GROUP_SIZE) < imagesList.length ? 'auto' : 'none';
  }
}

function setCurrentIndex(globalIndex, opts = { autoAdjustGroup: true, smoothScroll: false }){
  if (!imagesList || imagesList.length === 0) { currentIndex = -1; renderViewerEmpty(); return; }
  if (globalIndex < 0) globalIndex = 0;
  if (globalIndex > imagesList.length - 1) globalIndex = imagesList.length - 1;
  currentIndex = globalIndex;

  if (opts.autoAdjustGroup) {
    const desiredGroupStart = Math.floor(currentIndex / THUMB_GROUP_SIZE) * THUMB_GROUP_SIZE;
    if (desiredGroupStart !== thumbGroupStart) {
      thumbGroupStart = desiredGroupStart;
      renderThumbGroup();
    } else {
      updateThumbActiveState();
    }
  } else {
    updateThumbActiveState();
  }

  const name = imagesList[currentIndex];
  renderMainImage(name);
  
  if (opts.smoothScroll) {
    setTimeout(() => {
      const windowEl = document.getElementById('thumbWindow');
      if(!windowEl) return;
      const active = windowEl.querySelector(`.thumb-item[data-index='${currentIndex}']`);
      if (active) {
        const strip = document.getElementById('thumbStrip');
        if(strip) {
            active.scrollIntoView({behavior:'smooth', inline:'center', block:'nearest'});
        }
      }
    }, 60);
  }
}

function shiftThumbGroup(deltaGroups){
  const newStart = thumbGroupStart + deltaGroups * THUMB_GROUP_SIZE;
  thumbGroupStart = Math.max(0, Math.min(newStart, Math.max(0, imagesList.length - 1)));
  renderThumbGroup();
  if (currentIndex < thumbGroupStart || currentIndex >= thumbGroupStart + THUMB_GROUP_SIZE) {
    const newIndex = Math.min(imagesList.length - 1, thumbGroupStart + 0);
    setCurrentIndex(newIndex, {autoAdjustGroup:false});
  }
}

function renderMainImage(name){
  const inner = document.getElementById('viewerInner');
  if(!inner) return;
  inner.innerHTML = '';
  scale = 1; translate = {x:0,y:0};
  const img = document.createElement('img');
  img.src = '/uploads/' + encodeURIComponent(name);
  img.alt = name;
  img.draggable = false;
  
  img.onerror = function() {
    console.log('图片加载失败，重试...');
    setTimeout(() => {
      this.src = '/uploads/' + encodeURIComponent(name);
    }, 1000);
  };
  
  inner.appendChild(img);
  const overlay = document.getElementById('viewerOverlay');
  if(overlay) overlay.innerText = name;
  setupImageInteractions(inner, img);
  updateThumbActiveState();
}

function renderViewerEmpty(){
  const inner = document.getElementById('viewerInner');
  if(inner) inner.innerHTML = '<div style="color:var(--muted)">暂无图片，请上传或请求要图</div>';
  const overlay = document.getElementById('viewerOverlay');
  if(overlay) overlay.innerText = '-- 时间戳 --';
}

/* 缩放/拖动 */
function setupImageInteractions(container, imgEl){
  scale = 1; translate = {x:0,y:0}; isPanning = false;
  function applyTransform(){ imgEl.style.transform = `translate(${translate.x}px, ${translate.y}px) scale(${scale})`; }
  imgEl.ondblclick = () => { scale = 1; translate = {x:0,y:0}; applyTransform(); };
  container.onwheel = (ev) => {
    ev.preventDefault();
    const delta = -ev.deltaY;
    const step = delta > 0 ? 0.12 : -0.12;
    const newScale = Math.max(1, Math.min(4, scale + step));
    const rect = imgEl.getBoundingClientRect();
    const cx = ev.clientX - rect.left;
    const cy = ev.clientY - rect.top;
    const relX = (cx - rect.width/2 - translate.x) / scale;
    const relY = (cy - rect.height/2 - translate.y) / scale;
    translate.x -= (newScale - scale) * relX;
    translate.y -= (newScale - scale) * relY;
    scale = newScale;
    applyTransform();
  };
  container.onpointerdown = (ev) => {
    if (scale <= 1) return;
    isPanning = true;
    panStart.x = ev.clientX; panStart.y = ev.clientY;
    container.setPointerCapture(ev.pointerId);
  };
  container.onpointermove = (ev) => {
    if (!isPanning) return;
    const dx = ev.clientX - panStart.x;
    const dy = ev.clientY - panStart.y;
    panStart.x = ev.clientX; panStart.y = ev.clientY;
    translate.x += dx; translate.y += dy;
    applyTransform();
  };
  container.onpointerup = (ev) => {
    if (!isPanning) return;
    isPanning = false;
    try { container.releasePointerCapture(ev.pointerId); } catch(e){}
  };
  container.onpointercancel = () => { isPanning = false; };
}

function showPrev(){
  if (imagesList.length === 0) return;
  const newIndex = Math.max(0, currentIndex - 1);
  setCurrentIndex(newIndex, {autoAdjustGroup:true, smoothScroll:true});
}
function showNext(){
  if (imagesList.length === 0) return;
  const newIndex = Math.min(imagesList.length - 1, currentIndex + 1);
  setCurrentIndex(newIndex, {autoAdjustGroup:true, smoothScroll:true});
}

function updateChannelDisplay() {
// 在标题栏显示当前通道
  const subtitle = document.querySelector('.subtitle');
  if (subtitle) {
    const channelName = currentChannel === 7 ? '默认通道' : `通道 ${currentChannel}`;
    subtitle.textContent = `在线监控 · 全景要图 · 设备管理 · 当前: ${channelName}`;
  }
}

// 修改抓图按钮事件
const grabBtn = document.getElementById('grabBtn');
if (grabBtn) {
  grabBtn.addEventListener('click', async () => {
    const firstDeviceElem = document.querySelector('#devicesContainer .device strong');
    if (firstDeviceElem) {
      const dev = firstDeviceElem.innerText;
      
      const oldText = grabBtn.innerText;
      grabBtn.innerText = '发送中...';
      grabBtn.disabled = true;
      
      try {
        const response = await fetch('/api/send_b341', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ 
            device: dev, 
            channel: currentChannel  // 使用当前选中的通道
          })
        });
        
        const result = await response.json();
        if (result.ok) {
          showNotification(`已向设备 ${dev} 发送B341抓图指令 (通道${currentChannel})`, 'success');
          setTimeout(async () => {
            await loadImages(); // 重新加载当前通道的图片
            showNotification('正在检查新图片...', 'info');
          }, 3000);
        } else {
          showNotification(`发送失败: ${result.error || '未知错误'}`, 'error');
        }
      } catch (error) {
        showNotification('请求失败: ' + error.message, 'error');
      } finally {
        grabBtn.innerText = oldText;
        grabBtn.disabled = false;
      }
    } else {
      showNotification('无可用设备，请等待设备连接', 'error');
    }
  });
}

const prevBtn = document.getElementById('prevBtn');
const nextBtn = document.getElementById('nextBtn');
if(prevBtn) prevBtn.addEventListener('click', showPrev);
if(nextBtn) nextBtn.addEventListener('click', showNext);
window.addEventListener('keydown', (ev) => {
  if (ev.key === 'ArrowLeft') showPrev();
  if (ev.key === 'ArrowRight') showNext();
  if (ev.key === 'Escape') {
    const img = document.querySelector('#viewerInner img');
    if (img) { img.style.transform = 'translate(0px,0px) scale(1)'; scale = 1; translate = {x:0,y:0}; }
  }
});

const stripPrev = document.getElementById('stripPrev');
const stripNext = document.getElementById('stripNext');
if(stripPrev) stripPrev.addEventListener('click', () => shiftThumbGroup(-1));
if(stripNext) stripNext.addEventListener('click', () => shiftThumbGroup(1));

document.addEventListener('DOMContentLoaded', () => {
  const uploadForm = document.getElementById('uploadForm');
  if(uploadForm) {
      uploadForm.addEventListener('submit', async (ev) => {
        ev.preventDefault();
        const f = document.getElementById('fileInput').files[0];
        if (!f) { 
          showNotification('请选择文件', 'error'); 
          return; 
        }
        
        const fd = new FormData();
        fd.append('image', f, f.name);
        
        // 添加通道参数
        fd.append('channel', currentChannel);
        
        const resDiv = document.getElementById('uploadResult');
        if(resDiv) resDiv.innerText = '上传中...';
        try {
          const resp = await fetch('/upload', { 
            method: 'POST', 
            body: fd 
          });
          const j = await resp.json();
          if (j.ok) {
            if(resDiv) resDiv.innerText = '上传成功: ' + j.filename;
            showNotification('图片上传成功', 'success');
            await loadImages(); // 重新加载当前通道的图片
          } else {
            if(resDiv) resDiv.innerText = '上传失败: ' + (j.error||'未知');
            showNotification('上传失败: ' + (j.error||'未知'), 'error');
          }
        } catch (e) {
          if(resDiv) resDiv.innerText = '上传异常: ' + e.message;
          showNotification('上传异常: ' + e.message, 'error');
        }
      });
  }

  const downloadBtn = document.getElementById('downloadBtn');
  if(downloadBtn) {
      downloadBtn.addEventListener('click', () => {
        if (currentIndex >= 0 && imagesList[currentIndex]) {
          const url = '/uploads/' + encodeURIComponent(imagesList[currentIndex]);
          const a = document.createElement('a'); a.href = url; a.download = imagesList[currentIndex]; document.body.appendChild(a); a.click(); a.remove();
        } else showNotification('无图片可下载', 'error');
      });
  }
  
  const fullBtn = document.getElementById('fullBtn');
  if(fullBtn) {
      fullBtn.addEventListener('click', () => {
        const viewer = document.getElementById('viewerWrap');
        if (document.fullscreenElement) document.exitFullscreen();
        else if(viewer) viewer.requestFullscreen().catch(()=>showNotification('无法进入全屏','error'));
      });
  }

  document.querySelectorAll('.nav-tabs button').forEach(b => {
    b.addEventListener('click', () => {
      document.querySelectorAll('.nav-tabs button').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
      showNotification('切换到：' + b.innerText, 'info');
    });
  });

  // 初始化
  initCalendar();
  initChannels();
  initUpgradeModal();
  init();
});

// 在初始化函数中添加通道状态显示
async function init() {
  try {
    await loadConnectionStats();
    await loadDevices();
    await loadImages(); // 这会加载当前通道的图片
    
    // 在标题栏显示当前通道
    updateChannelDisplay();
    
    refreshInterval = setInterval(async () => { 
      await loadDevices(); 
      await loadConnectionStats(); 
    }, 5000);
  } catch (e) {
    console.error('初始化异常', e);
  }
}

async function loadConnectionStats(){
  try {
    const data = await fetchJSON('/api/connections');
    document.title = `设备管理 (${data.total_connections||0} 连接)`;
  } catch(e){}
}

function showNotification(message, type='info'){
  const existing = document.getElementById('notification');
  if (existing) existing.remove();
  const n = document.createElement('div');
  n.id = 'notification';
  n.style.position = 'fixed';
  n.style.top = '18px'; n.style.right = '18px';
  n.style.padding = '12px 16px'; n.style.borderRadius = '8px';
  n.style.zIndex = 9999; n.style.boxShadow = '0 8px 30px rgba(0,0,0,0.15)';
  n.style.fontWeight = 700; n.style.color = '#ffffff';
  if (type === 'success') n.style.background = 'linear-gradient(#2e7d32,#1b5e20)';
  else if (type === 'error') n.style.background = 'linear-gradient(#d32f2f,#b71c1c)';
  else n.style.background = 'linear-gradient(#4caf50,#2e7d32)';
  n.innerText = message;
  document.body.appendChild(n);
  setTimeout(() => { n.style.opacity = '0'; n.style.transform = 'translateX(60px)'; setTimeout(()=>n.remove(),300); }, 2500);
}

window.addEventListener('beforeunload', () => { if (refreshInterval) clearInterval(refreshInterval); });



// ---------------- 推理弹窗相关 ----------------
const inferModalOpenBtn = document.getElementById('inferModalOpenBtn');
const inferModal = document.getElementById('inferModal');
const inferModalClose = document.getElementById('inferModalClose');
const cancelInfer = document.getElementById('cancelInferModal');
const startInferBtn = document.getElementById('startInferModal');
const inferPreview = document.getElementById('inferPreview');
const inferImgLabel = document.getElementById('inferImgLabel');
const inferLog = document.getElementById('inferLogModal');
const inferResultImage = document.getElementById('inferResultImage');
const inferResultLabel = document.getElementById('inferResultLabel');
const mainImageEl = document.getElementById('mainImage');

// Multi-image support
let selectedInferImageBlob = null;
let selectedInferImageBlob2 = null;
const inferPreview2 = document.getElementById('inferPreview2');
const inferImgLabel2 = document.getElementById('inferImgLabel2');
const image2Group = document.getElementById('image2Group');
const inferBeforeImage = document.getElementById('inferBeforeImage');
const inferBeforeLabel = document.getElementById('inferBeforeLabel');

function appendInferLog(msg, type='info') {
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    const timeStr = new Date().toLocaleTimeString();
    entry.innerText = `[${timeStr}] ${msg}`;
    if (type === 'error') entry.style.color = '#ff5252';
    else if (type === 'success') entry.style.color = '#69f0ae';
    inferLog.appendChild(entry);
    inferLog.scrollTop = inferLog.scrollHeight;
}

// Model selection change handler
const inferModelSelectModal = document.getElementById('inferModelSelectModal');
if (inferModelSelectModal) {
    inferModelSelectModal.addEventListener('change', (e) => {
        const modelType = e.target.value;
        if (modelType === 'hdr_fusion') {
            // Show second image upload for HDR fusion
            image2Group.style.display = 'block';
        } else {
            // Hide second image upload for single-image models
            image2Group.style.display = 'none';
            selectedInferImageBlob2 = null;
            inferPreview2.style.display = 'none';
            inferPreview2.removeAttribute('src');
            inferImgLabel2.innerText = "请选择第二张图片";
            inferImgLabel2.style.display = 'inline';
        }
    });
}

if (inferModalOpenBtn) {
    inferModalOpenBtn.addEventListener('click', () => {
        inferModal.style.display = 'flex';
        inferLog.innerHTML = '<div class="log-entry info">准备推送到多模态推理微服务...</div>';
        selectedInferImageBlob = null;
        selectedInferImageBlob2 = null;

        // Reset file inputs
        const fileInput = document.getElementById('inferImageFile');
        if (fileInput) fileInput.value = '';
        const fileInput2 = document.getElementById('inferImageFile2');
        if (fileInput2) fileInput2.value = '';

        // Reset previews
        inferPreview.style.display = 'none';
        inferPreview.removeAttribute('src');
        inferImgLabel.innerText = "请先选择图片";
        inferImgLabel.style.display = 'inline';

        inferPreview2.style.display = 'none';
        inferPreview2.removeAttribute('src');
        inferImgLabel2.innerText = "请选择第二张图片";
        inferImgLabel2.style.display = 'inline';

        inferBeforeImage.style.display = 'none';
        inferBeforeImage.removeAttribute('src');
        inferBeforeLabel.innerText = "等待上传";
        inferBeforeLabel.style.display = 'inline';

        inferResultImage.style.display = 'none';
        inferResultImage.removeAttribute('src');
        inferResultLabel.innerText = "等待执行推理";
        inferResultLabel.style.display = 'inline';

        // Reset model selection
        const modelSelect = document.getElementById('inferModelSelectModal');
        if (modelSelect) modelSelect.value = 'yolo';
        image2Group.style.display = 'none';
    });
}

// Add File Input changed event for image 1
const inferImageFile = document.getElementById('inferImageFile');
if (inferImageFile) {
    inferImageFile.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            selectedInferImageBlob = e.target.files[0];
            const reader = new FileReader();
            reader.onload = function(evt) {
                inferPreview.src = evt.target.result;
                inferPreview.style.display = 'block';
                inferImgLabel.style.display = 'none';
                // Also show in "Before" panel
                inferBeforeImage.src = evt.target.result;
                inferBeforeImage.style.display = 'block';
                inferBeforeLabel.style.display = 'none';
            }
            reader.readAsDataURL(e.target.files[0]);
        }
    });
}

// Add File Input changed event for image 2
const inferImageFile2 = document.getElementById('inferImageFile2');
if (inferImageFile2) {
    inferImageFile2.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            selectedInferImageBlob2 = e.target.files[0];
            const reader = new FileReader();
            reader.onload = function(evt) {
                inferPreview2.src = evt.target.result;
                inferPreview2.style.display = 'block';
                inferImgLabel2.style.display = 'none';
            }
            reader.readAsDataURL(e.target.files[0]);
        }
    });
}

function closeInferModal() {
    inferModal.style.display = 'none';
}

if(inferModalClose) inferModalClose.addEventListener('click', closeInferModal);
if(cancelInfer) cancelInfer.addEventListener('click', closeInferModal);

if (startInferBtn) {
    startInferBtn.addEventListener('click', async () => {
        if (!selectedInferImageBlob) {
            appendInferLog("错误: 请先选择图片进行推理", "error");
            return;
        }

        const modelType = document.getElementById('inferModelSelectModal').value;
        const modelName = document.getElementById('inferModelSelectModal').options[document.getElementById('inferModelSelectModal').selectedIndex].text;

        // Check for HDR fusion requiring 2 images
        if (modelType === 'hdr_fusion' && !selectedInferImageBlob2) {
            appendInferLog("错误: 多曝光融合需要2张图片(低曝光+高曝光)", "error");
            return;
        }

        startInferBtn.disabled = true;
        startInferBtn.innerText = '推理中...';
        appendInferLog(`开始启动 ${modelName} 任务...`, 'info');

        try {
            let formData = new FormData();
            formData.append('image1', selectedInferImageBlob, 'image1.jpg');
            formData.append('model', modelType);

            // Add second image if exists (for HDR fusion)
            if (selectedInferImageBlob2) {
                formData.append('image2', selectedInferImageBlob2, 'image2.jpg');
            }

            const inferRes = await fetch('/api/infer', {
                method: 'POST',
                body: formData
            });

            const dataText = await inferRes.text();

            if (inferRes.ok) {
                appendInferLog(`推理微服务返回成功！`, 'success');
                try {
                    const resJson = JSON.parse(dataText);
                    appendInferLog(`耗时 ${resJson.inference_time_ms} ms, 目标数量: ${resJson.detections?.length || 0}`, "info");
                    if (resJson.image_b64) {
                        inferResultImage.src = resJson.image_b64;
                        inferResultImage.style.display = 'block';
                        inferResultLabel.style.display = 'none';
                    }
                } catch(e) {
                    appendInferLog("JSON 解析结果失败: " + e.message, "error");
                }
                showNotification('微服务推理成功！', 'success');
            } else {
                appendInferLog(`请求网关失败码: ${inferRes.status}`, 'error');
                appendInferLog(dataText, 'error');
                showNotification('推理失败: ' + inferRes.status, 'error');
            }
        } catch(err) {
            appendInferLog("网络异常: " + err.message, "error");
            showNotification('请求推理接口异常', 'error');
        } finally {
            startInferBtn.disabled = false;
            startInferBtn.innerText = '开始推理';
        }
    });
}

