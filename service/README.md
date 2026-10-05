# Zalo10Service — service headless nhận thông báo khi app đã đóng

Dùng lại **nguyên** `ZaloService` của app UI (WebSocket, giải mã, DB, Hub) trong 1 process nền
không có UI. Chưa build/test trên máy thật — phần dưới là các bước dựng + việc cần kiểm chứng.

## Cách hoạt động
- Zalo chỉ cho **1 kết nối WebSocket / tài khoản** → tại 1 thời điểm chỉ 1 process giữ realtime.
- UI mở: ghi `zalo10_ui.pid` (trong data dir) rồi ping service (`UI_OPENED`) → service gọi
  `suspendRealtime()` (đóng WS, giữ session).
- UI đóng (kể cả vuốt đóng/SIGTERM): xoá file PID rồi ping (`UI_CLOSED`) → service gọi
  `resumeRealtime()` = `loadSession()` → refresh secretKey → nối WS → tin đến đi vào Hub.
- UI bị kill -9: file PID mồ côi, `kill(pid,0)` báo process chết → service tự nhận lại WS
  trong ≤ 4 giây (poll).
- Phiên hết hạn: service đứng yên (không có UI để hiện QR) tới khi user mở app đăng nhập lại.

## Dựng project trong Momentics
1. Tạo project **BlackBerry C++ → Headless/Service** tên `Zalo10Service` cạnh project `Zalo10`
   (template sinh sẵn Makefile/.cproject đúng cấu hình QNX).
2. Thay `src/` của template bằng 3 file trong thư mục này (`main.cpp`, `ServiceController.*`,
   `ContactPickerStub.cpp`) và dùng `Zalo10Service.pro` này (nó trỏ sang `../Zalo10/src`).
3. Trong `bar-descriptor.xml` của **Zalo10** (UI):
   - mỗi `<configuration>` thêm asset binary của service, dạng
     `<asset path="../Zalo10Service/arm/o.le-v7-g/Zalo10Service" type="Qnx/Elf">Zalo10Service</asset>`
     (đổi `arm/o.le-v7-g` theo từng cấu hình: `arm/o.le-v7`, `x86/o-g`…). Đối chiếu cách khai báo
     với bar-descriptor mà template Headless sinh ra.
   - thêm invoke-target của service (các permission `run_when_backgrounded`, `_sys_run_headless`,
     `_sys_headless_nostop`, `post_notification`, `access_internet` đã có sẵn):

```xml
<invoke-target id="com.BerryLife.Zalo10.service">
    <invoke-target-name>Zalo10Service</invoke-target-name>
    <icon><image>icon.png</image></icon>
    <type>service</type>
    <filter>
        <action>bb.action.system.STARTED</action>
        <mime-type>application/vnd.blackberry.system.event.STARTED</mime-type>
        <property var="uris" value="data://local"/>
    </filter>
    <filter>
        <action>com.BerryLife.Zalo10.service.UI_OPENED</action>
        <action>com.BerryLife.Zalo10.service.UI_CLOSED</action>
        <mime-type>text/plain</mime-type>
        <property var="uris" value="data://local"/>
    </filter>
</invoke-target>
```
4. Build service trước, rồi build/đóng gói UI.

## Cần kiểm chứng trên máy thật (theo thứ tự)
1. Service chạy được, có log `/accounts/1000/appdata/<app>/data/zalo10_service.log`
   (hoặc `~/zalo10_service.log`), WS nối được với session đã lưu.
2. **Khi 2 process cùng nối thì Zalo làm gì** (đá kết nối cũ? mã đóng nào?) — code hiện chưa xử lý mã
   đóng đặc biệt. Nếu hai bên giành nhau thì thêm xử lý mã đó trong `onWsDisconnected()`.
3. Tin đến khi UI đóng hiện được trong Hub; bấm vào Hub mở UI, UI đồng bộ lại tin.
4. Thời gian chuyển giao UI mở/đóng, mức pin và RAM của service.
5. `QImage` trong QCoreApplication (không có QApplication) và `HubIntegration` (UDS) chạy ổn
   trong process headless.
6. SIGTERM: service chưa có handler `saveSession()` riêng (chỉ lưu theo keepalive). Thêm nếu cần.
