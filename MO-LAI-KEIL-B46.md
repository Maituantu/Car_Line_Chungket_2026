# Khôi phục project B46

Bản này lưu source B46 tại commit `63daf6c87c8d89870feae049ea3a4cb684c6a2a0`,
project Keil, SPL/CMSIS, startup, cấu hình Debug hiện tại, HEX/AXF/MAP và tài liệu.
Các source baseline cần cho test comparator cũng được lưu trong `backups/`.

## Mở lại bằng Keil

1. Giải nén toàn bộ ZIP vào một thư mục, ví dụ `D:\STM32-B46\`.
2. Dùng Keil µVision 5, ARM Compiler **5.06 update 6**, Device Pack
   **Keil.STM32F1xx_DFP 2.4.1** như môi trường build gốc.
3. Mở **`linefollower.uvprojx`** trong thư mục vừa giải nén, chọn target **LineFollower**.
4. Chọn **Rebuild** để build source; output nằm trong `build/`.
5. Kết nối ST-Link, kiểm tra adapter trong Options for Target → Debug và
   Utilities theo adapter trên máy, rồi chọn Download để nạp.

Project dùng đường dẫn source tương đối; giữ nguyên cấu trúc thư mục khi giải nén.
Keil/compiler/Device Pack được cài trên máy, không nằm trong ZIP.
File HEX có sẵn: **`build/linefollower-B46.hex`**.

## Nhận diện bản firmware

- Firmware tag: **B46**.
- Defaults: **Base 55 / Max 85, Kp 12 / Ki 0 / Kd 0,90**.
- Keil gốc đã build **0 Error / 0 Warning**; toàn bộ test suite pass.
- HEX SHA-256: `9B5599FBE2E0C7D54405FFADC4CE89554C20882B301837DFAA2F73A7375C240E`.
- Chi tiết thuật toán: `docs/curve-slow-b46.md`.

ZIP lưu firmware và source. Calibration/PID đang nằm trong RAM hoặc Flash
trên chiếc STM32 của bạn không được đọc vào bản backup này; cần kiểm tra/calib
lại khi thay board hoặc xóa Flash. B45 là bản owner báo 15,2s ổn định 5/5 lượt;
B46 chưa có kết quả đo lap được gửi lại tại thời điểm đóng gói.

`BACKUP-MANIFEST.json` chứa commit và SHA-256 cho từng file để đối chiếu.
