import requests

with open('client/bin/ImageSend', 'rb') as f:
    files = {'image': ('test.jpg', f, 'image/jpeg')}
    data = {'model': 'yolo'}
    r = requests.post('http://127.0.0.1:8080/api/infer', files=files, data=data)
    print(r.status_code)
    print(r.text)
