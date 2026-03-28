import re

with open('server/frontend/js/app.js', 'r', encoding='utf-8') as f:
    js_content = f.read()

# Replace the inferModalOpenBtn logic using simpler regex since it spans multiple lines. The previous dotall might have failed if async was present.
new_open_btn = """
if (inferModalOpenBtn) {
    inferModalOpenBtn.addEventListener('click', () => {
        inferModal.style.display = 'flex';
        inferLog.innerHTML = '<div class="log-entry info">准备推送到多模态推理微服务...</div>';
        selectedInferImageBlob = null;
        
        // Reset file input
        const fileInput = document.getElementById('inferImageFile');
        if (fileInput) fileInput.value = '';
        
        inferPreview.style.display = 'none';
        inferPreview.removeAttribute('src');
        inferImgLabel.innerText = "请从上方选择图片进行推理";
    });
}

// Add File Input changed event to preview
const inferImageFile = document.getElementById('inferImageFile');
if (inferImageFile) {
    inferImageFile.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            selectedInferImageBlob = e.target.files[0];
            const reader = new FileReader();
            reader.onload = function(evt) {
                inferPreview.src = evt.target.result;
                inferPreview.style.display = 'block';
                inferImgLabel.innerText = "已选择: " + e.target.files[0].name;
            }
            reader.readAsDataURL(e.target.files[0]);
        }
    });
}

function closeInferModal() {
"""

# Hard substitution using split
parts = js_content.split('if (inferModalOpenBtn) {')
end_parts = parts[1].split('function closeInferModal() {')
js_content = parts[0] + new_open_btn + end_parts[1]

with open('server/frontend/js/app.js', 'w', encoding='utf-8') as f:
    f.write(js_content)
