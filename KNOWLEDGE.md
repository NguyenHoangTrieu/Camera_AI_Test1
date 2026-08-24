# KNOWLEDGE.md - Kiến thức kỹ thuật đúc kết từ project này

File này khác với [WORKLOG.md](WORKLOG.md) (nhật ký từng bug, theo dòng thời
gian) và [README.md](README.md) (hướng dẫn build/chạy). Đây là phần **giải
thích khái niệm** - đọc file này để hiểu *tại sao* mọi thứ hoạt động như vậy,
không cần biết trước gì về SmartDMA, camera DVP, TFLite Micro hay NPU.

Mục lục:
1. [Camera: giao thức DVP và SmartDMA](#1-camera-giao-thức-dvp-và-smartdma)
2. [Bản đồ bộ nhớ trên MCXN947 (vì sao quan trọng)](#2-bản-đồ-bộ-nhớ-trên-mcxn947)
3. [AI: Edge Impulse FOMO là gì, tích hợp ra sao](#3-ai-edge-impulse-fomo-là-gì-tích-hợp-ra-sao)
4. [Kỹ năng debug: đọc thanh ghi Fault đúng cách](#4-kỹ-năng-debug-đọc-thanh-ghi-fault-đúng-cách)
5. [NPU: Neutron là gì, init và dùng thế nào](#5-npu-neutron-là-gì-init-và-dùng-thế-nào)
6. [USB: tại sao bỏ UVC streaming](#6-usb-tại-sao-bỏ-uvc-streaming)
7. [Bài học tổng quát](#7-bài-học-tổng-quát)

---

## 1. Camera: giao thức DVP và SmartDMA

### DVP là gì?

DVP (Digital Video Port) là kiểu giao tiếp camera **song song** cổ điển, khác
hẳn với camera MIPI-CSI hiện đại. Camera OV7670 trong project này dùng DVP:

- **8 chân dữ liệu song song** (D0..D7) - mỗi xung clock đẩy ra 1 byte pixel
  luôn, không cần giải mã serial.
- **PCLK** (pixel clock) - camera tự tạo, báo hiệu "dữ liệu đã sẵn sàng trên
  D0-D7, đọc đi".
- **HREF** (horizontal reference) - cao trong lúc đang truyền 1 dòng pixel.
- **VSYNC** (vertical sync) - đánh dấu bắt đầu/kết thúc 1 khung hình.
- **SCCB** (2 dây, giống I2C) - kênh **điều khiển** riêng biệt, dùng để cấu
  hình camera (độ phân giải, exposure, mirror/flip...), **không** truyền
  pixel qua đây.

Vì camera tự tạo PCLK/HREF/VSYNC theo tốc độ riêng của nó (không đồng bộ với
CPU), việc bắt kịp và chép đúng từng byte vào RAM bằng phần mềm thuần (busy-
loop đọc GPIO) gần như bất khả thi ở tốc độ thực tế (320x240 @ 30fps = ~2.3
triệu byte/giây). Đây là lý do cần SmartDMA.

### SmartDMA là gì?

SmartDMA là một **coprocessor DMA có thể lập trình** riêng trên các chip MCX
của NXP (khác hẳn DMA thường - DMA thường chỉ copy vùng nhớ A sang B theo
lệnh cố định, SmartDMA chạy hẳn 1 đoạn **firmware/microcode** để tự quyết
định khi nào đọc dữ liệu, xử lý logic HREF/VSYNC, v.v). Nó có:

- **CPU lõi riêng** (không phải Cortex-M33 chính) chạy firmware riêng.
- **Vùng RAM làm việc riêng** để chứa firmware + trạng thái khi đang chạy -
  đây chính là `m_sramx`, xem phần 2 vì đây là nguồn gốc 1 bug lớn của
  project này.

Luồng khởi động camera trong `camera_capture.c`:

```c
SMARTDMA_InstallFirmware(SMARTDMA_CAMERA_MEM_ADDR, s_smartdmaCameraFirmware, ...);
SMARTDMA_InstallCallback(CAMERA_CAPTURE_CompleteCallback, NULL);
NVIC_EnableIRQ(SMARTDMA_IRQn);
SMARTDMA_Boot(DEMO_SMARTDMA_API, &smartdmaParam, 0x2);
```

- `SMARTDMA_InstallFirmware()` **nạp** đoạn microcode camera-capture vào
  `SMARTDMA_CAMERA_MEM_ADDR` (địa chỉ cố định `0x04000000`).
- `SMARTDMA_Boot(kSMARTDMA_CameraWholeFrameQVGA, ...)` khởi động coprocessor
  chạy firmware đó, với chế độ "mỗi khung hình đầy đủ (320x240) tạo 1 ngắt".
- Khi SmartDMA chép xong 1 khung hình vào buffer, nó bắn **ngắt phần cứng**
  (`SMARTDMA_IRQn`) -> `CAMERA_CAPTURE_CompleteCallback()` chạy -> đặt cờ
  `s_frameReady = true`.
- CPU chính (Cortex-M33) chỉ cần **poll cờ đó** trong vòng lặp `main()`, hoàn
  toàn không phải bận tâm timing pixel-by-pixel - SmartDMA lo hết.

Đây là mô hình chuẩn: **coprocessor lo phần thời gian-thực khắt khe (bắt
pixel), CPU chính lo phần logic (AI, hiển thị)**.

### Bài học quan trọng: SmartDMA cần RAM riêng, không phải RAM "rảnh"

`SMARTDMA_CAMERA_MEM_ADDR = 0x04000000` trùng khớp **chính xác** với địa chỉ
bắt đầu của `m_sramx` - một bank RAM 96KB trên chip mà file linker script gốc
của SDK **không khai báo output section nào cho nó** (vì SmartDMA nạp
firmware vào đó lúc chạy, qua hàm C, không phải qua linker tĩnh).

Điều này khiến `m_sramx` **trông như "chưa ai dùng"** nếu chỉ nhìn linker
script - dẫn đến quyết định (sai) là dùng nó làm vùng nhớ cho AI tensor
arena. Hậu quả: ngay khi AI bắt đầu ghi dữ liệu thật vào đó, nó ghi đè lên
microcode/trạng thái SmartDMA đang chạy, khiến camera **âm thầm ngừng gửi
khung hình mới** sau vài frame đầu (không crash, không log lỗi gì - chỉ đơn
giản là không còn ngắt nào bắn ra nữa). Xem chi tiết đầy đủ ở WORKLOG.md
"Bug #3".

**Bài học chung:** một vùng RAM "không có gì trong linker map" không đồng
nghĩa với "không ai dùng nó lúc chạy" - nếu chip có coprocessor riêng
(SmartDMA, NPU, DSP phụ...), luôn kiểm tra tài liệu xem coprocessor đó có
cắm cố định vào bank RAM nào không, trước khi tận dụng bank đó cho việc
khác.

**Cách giải quyết đã áp dụng:** dừng hẳn SmartDMA (`CAMERA_CAPTURE_Deinit()`)
trước khi AI bắt đầu ghi vào `m_sramx`, khởi động lại
(`CAMERA_CAPTURE_Reinit()`) sau khi AI chạy xong. Camera và AI không bao giờ
"đụng" `m_sramx` cùng lúc - đổi lại là capture/inference chạy tuần tự, không
song song (chấp nhận được vì AI vốn đã là bước chậm nhất trong pipeline).

---

## 2. Bản đồ bộ nhớ trên MCXN947

Chip này có **nhiều bank RAM vật lý tách biệt**, không phải 1 khối RAM liền
mạch như nhiều MCU đơn giản hơn:

| Vùng | Kích thước | Dùng cho gì trong project này |
|---|---|---|
| `m_data` | 312KB | RAM "chính" - biến toàn cục, stack, heap, framebuffer camera/LCD, tensor arena của bản NPU |
| `m_sramx` | 96KB | Bank riêng SmartDMA dùng làm RAM firmware/trạng thái khi capture camera; cũng là nơi bản CPU (CMSIS-NN) đặt tensor arena AI (khi camera đã dừng, xem mục 1) |
| `m_text` | 767KB (flash) | Code (.text), hằng số |

**Vì sao phải quan tâm chuyện này:** hầu hết MCU tầm trung chỉ có 1 vùng SRAM
duy nhất, lập trình viên hiếm khi phải nghĩ về "bank nào". Trên chip có nhiều
bank tách biệt vật lý (thường để tối ưu băng thông cho DMA/coprocessor chạy
song song với CPU), một biến `static uint8_t buf[1000]` đặt sai bank có thể:
- Chạy chậm hơn nếu bank đó không được CPU truy cập tối ưu.
- **Đụng độ vật lý** với coprocessor khác đang dùng chính bank đó (bug #3 ở
  trên).

Trong project này, việc "ngân sách RAM" (RAM budget) được nhắc đi nhắc lại
nhiều lần trong WORKLOG.md không phải vì thiếu RAM tổng thể, mà vì **từng
bank riêng lẻ có giới hạn cứng của nó** - dồn quá nhiều vào 1 bank sẽ tràn dù
bank khác còn dư rất nhiều.

Ví dụ cụ thể đã gặp: stack CPU cần tăng lên 16KB để chạy được TFLite
Micro/CMSIS-NN (gọi hàm lồng sâu, mỗi frame tốn nhiều byte cục bộ) - nhưng
`m_data` gần đầy (~99%) vì một buffer overflow-pool 150KB không cần thiết
chiếm chỗ. Giải pháp không phải "mua thêm RAM" mà là dọn bớt buffer thừa để
nhường chỗ cho stack.

---

## 3. AI: Edge Impulse FOMO là gì, tích hợp ra sao

### FOMO là gì?

FOMO (**F**aster **O**bjects, **M**ore **O**bjects) là kiến trúc phát hiện
vật thể (object detection) của Edge Impulse, thiết kế riêng cho MCU yếu -
khác hẳn YOLO/SSD (vốn cần GPU/NPU mạnh):

- Backbone: MobileNetV2 **cắt ngắn** (chỉ giữ vài block đầu, alpha=0.35 - hệ
  số thu nhỏ số kênh) → ảnh input 64x64 sau vài lớp conv/depthwise-conv co
  lại còn lưới **8x8** (mỗi ô lưới coi như 1 "cell" đại diện vùng 8x8 pixel
  gốc).
- Không có "bounding box regression" như YOLO - mỗi ô lưới chỉ trả lời:
  "ô này thuộc lớp nào?" (kể cả lớp ẩn "background" = không có gì). Ô nào
  không phải background thì được coi là "phát hiện tại vị trí đó".
- Các ô liền kề cùng lớp được **gộp lại thành 1 box** (không phải model tự
  học ra box, mà thuật toán hậu xử lý ghép các ô lại) - xem
  `ei_handle_cube()`/`process_cubes()` trong Edge Impulse SDK, đã được
  port lại thủ công cho bản NPU (mục 5).

Vì không cần bước "regression tọa độ box" phức tạp, FOMO nhẹ hơn nhiều so
với YOLO - đây là lý do nó chạy nổi trên MCU Cortex-M33 thuần (dù chậm,
~1.27s/lần).

### Tích hợp Edge Impulse SDK vào project

Edge Impulse Studio xuất ra 1 thư viện C++ (`source/ai/edge_impulse/`) gồm:
- Model đã huấn luyện, dạng `.tflite` (flatbuffer) + code C++ bọc quanh nó.
- Một bản TensorFlow Lite Micro **được đóng gói riêng** (không dùng chung
  với TFLM "chính chủ" của mcuxsdk - quan trọng, xem mục 5).
- CMSIS-NN (thư viện kernel tăng tốc tính toán mạng nơ-ron trên lõi ARM
  Cortex-M, dùng tập lệnh DSP/SIMD của chip).

Hàm vào chính là `ei_run_classifier(signal_t *signal, ei_impulse_result_t *result, bool debug)`.
`signal_t` là 1 struct chứa **con trỏ hàm callback** để SDK tự "kéo" dữ liệu
ảnh khi cần (không phải mình đẩy dữ liệu vào) - cách này giúp SDK không cần
biết trước ảnh nguồn ở định dạng gì (RGB565, JPEG, RAW...), mình tự viết
callback để convert khi được gọi.

### 2 bug lớn nhất khi tích hợp (đáng nhớ vì rất dễ tái diễn ở project khác)

**Bug #1 - sai kích thước signal:** `signal.total_length` phải là **kích
thước ảnh input của model** (64×64=4096 pixel), không phải kích thước ảnh
camera gốc (320×240=76800 pixel). SDK **không tự resize** - nó đọc đúng
`total_length` phần tử qua callback rồi ghi thẳng vào buffer đã cấp phát
theo kích thước model. Nếu truyền sai (kích thước camera), SDK ghi tràn
buffer ~19 lần kích thước thật, gây lỗi bộ nhớ ở rất xa vị trí thực sự sai
(rất khó truy vết nếu không biết trước cơ chế này). **Việc resize ảnh phải
tự làm trong hàm callback**, không phải nhiệm vụ của SDK.

**Bug #2 - tưởng là lỗi alignment, thực ra là tràn stack:** xem mục 4 -
đây là bài học về cách đọc thanh ghi fault cho đúng, không nhắc lại ở đây.

### Bộ nhớ cấp phát động tùy biến (`ei_sramx_alloc.c`)

SDK dùng `ei_malloc()`/`ei_free()` nội bộ để cấp tensor arena + vài buffer
tạm khi resize ảnh. Mặc định các hàm này map thẳng vào `malloc()`/`free()`
chuẩn của libc, chạy trên heap `m_data` - nhưng `m_data` không đủ chỗ cho
tensor arena (~93KB). Giải pháp: viết đè các hàm `ei_malloc`/`ei_calloc`/
`ei_free` (chúng được khai báo `__attribute__((weak))` trong SDK, tức là có
thể override từ file khác), cấp phát từ `m_sramx` thay vì heap thật - đây
chính là `ei_sramx_alloc.c`, một bộ cấp phát kiểu "bump allocator" (chỉ tăng
con trỏ offset, không quét free-list) kèm 1 ngăn xếp nhỏ ghi lại các lần
cấp phát để `ei_free()` có thể hoàn tác đúng thứ tự (LIFO - vào sau ra
trước), khớp với cách SDK thực sự dùng (cấp phát lồng nhau kiểu ngăn xếp
khi resize ảnh theo từng dải nhỏ).

---

## 4. Kỹ năng debug: đọc thanh ghi Fault đúng cách

Đây là bài học **tốn nhiều thời gian nhất** trong cả project, đáng ghi lại kỹ.

Khi ARM Cortex-M gặp lỗi nghiêm trọng (truy cập bộ nhớ sai, tràn stack,
lệnh không hợp lệ...), nó nhảy vào **HardFault**, và ghi chi tiết nguyên
nhân vào thanh ghi `CFSR` (Configurable Fault Status Register) - 32-bit,
chia làm 3 phần:

```
CFSR (32 bit) = [ UFSR (16 bit, cao) | BFSR (8 bit, giữa) | MMFSR (8 bit, thấp) ]
                   bit 16-31             bit 8-15            bit 0-7
```

**Sai lầm đã mắc:** đọc `CFSR = 0x00100000` rồi đoán bừa đó là "bit thứ 8 gì
đó của UFSR" → kết luận là lỗi **UNALIGNED** (truy cập bộ nhớ không đúng
căn lề). Sai! Bit 8 của UFSR tương ứng bit (16+8)=24 của CFSR
(`0x01000000`), khác hẳn `0x00100000` (bit 20 của CFSR = bit 4 của UFSR).

**Bit 4 của UFSR là `STKOF`** (Stack Overflow - chỉ có trên lõi ARMv8-M như
Cortex-M33, không có trên M0/M3/M4 đời cũ) - phần cứng tự kiểm tra mỗi khi
`SP` (stack pointer) bị trừ xuống, nếu vượt quá giới hạn (`PSPLIM`/`MSPLIM`)
thì bắn fault **ngay lập tức**, trước khi lệnh đó kịp gây hư hại gì thêm.

**Cách xác nhận đúng, không đoán:** dịch ngược (disassemble) đúng địa chỉ
PC lúc fault:
```
arm-none-eabi-objdump -d --start-address=<PC> --stop-address=<PC+0x20> firmware.elf
```
Nếu thấy lệnh dạng `subw sp, sp, #<số lớn>` (đang cấp phát biến cục bộ trên
stack) đúng ngay tại PC gây fault → gần như chắc chắn là STKOF, vì đó là
lệnh **số học thuần túy** (không truy cập bộ nhớ), không thể nào gây lỗi
UNALIGNED được.

**Quy tắc chung khi debug fault trên ARM Cortex-M:**
1. Luôn tra đúng vị trí bit trong `core_cmXX.h` (CMSIS header của chip) -
   đừng đoán theo cảm tính "nhìn số thấy giống bit nào".
2. `MMFAR`/`BFAR` (địa chỉ gây lỗi) **chỉ có giá trị hợp lệ tương ứng đúng
   loại fault** (MemManage/BusFault) - với UsageFault (bao gồm STKOF), 2
   thanh ghi này **không mang ý nghĩa gì**, đọc chúng chỉ gây nhiễu thêm.
3. Dịch ngược địa chỉ PC lúc fault luôn là cách xác nhận đáng tin cậy nhất -
   nhanh hơn nhiều so với việc thử-sai (thử fix rồi build lại xem hết lỗi
   chưa) khi nguyên nhân thực sự chưa rõ.

---

## 5. NPU: Neutron là gì, init và dùng thế nào

### Neutron NPU là gì?

Neutron là dòng lõi tăng tốc mạng nơ-ron (NPU - Neural Processing Unit) của
NXP, tích hợp sẵn trên một số chip MCX/i.MX (bao gồm MCXN947, biến thể
"Neutron16"/target tên `mcxn94x`). Giống SmartDMA (mục 1), đây cũng là 1
**coprocessor độc lập** - có driver + firmware riêng
(`libNeutronDriver.a`/`libNeutronFirmware.a`), chạy song song CPU chính,
chuyên tăng tốc các phép nhân ma trận/tích chập (conv, depthwise-conv...)
vốn chiếm phần lớn thời gian tính toán của mạng nơ-ron.

### Model cần "biên dịch" riêng cho NPU - không phải chỉ bật cờ

Đây là điểm khác biệt lớn nhất so với CMSIS-NN (chạy trên CPU): **CMSIS-NN
tăng tốc bất kỳ model TFLite int8 chuẩn nào** (chỉ cần build đúng cờ), nhưng
**NPU cần model được convert riêng** bằng công cụ `neutron_converter`
(gói `eiq_neutron_sdk`, cài từ index PyPI riêng của NXP - không có trên
PyPI công khai):

```bash
neutron_converter --input model.tflite --target mcxn94x --output model_npu.tflite
```

Công cụ này quét graph model, tìm các cụm lớp (conv/pool/add...) mà NPU hỗ
trợ, **gộp chúng thành 1 "custom op" duy nhất tên `NEUTRON_GRAPH`** (chứa
sẵn microcode biên dịch cho đúng lõi Neutron16), các lớp NPU không hỗ trợ
(hiếm, ví dụ Softmax) thì giữ nguyên chạy CPU như cũ. Với model FOMO của
project này: **31/32 lớp gộp được vào NPU, chỉ Softmax chạy CPU** - tỷ lệ
tăng tốc rất cao vì gần như toàn bộ phần nặng (backbone MobileNetV2) đều
chạy NPU.

### Tích hợp vào code: TFLite Micro thuần, không qua Edge Impulse SDK

Vì Edge Impulse SDK (`ei_run_classifier()`) **không biết gì về custom op
`NEUTRON_GRAPH`** (SDK tự dựng resolver riêng, không cho mình chèn thêm op
lạ vào), không thể "bật NPU" trong cùng đường code CPU. Giải pháp: viết hẳn
1 đường code song song (`model_runner_npu.cpp`), gọi thẳng TensorFlow Lite
Micro API - không đi qua Edge Impulse nữa:

```cpp
// Chỉ cần đúng 2 op cho model FOMO này - trình neutron_converter
// tự in gợi ý này ra ngay trong file header nó sinh ra:
static tflite::MicroMutableOpResolver<2> s_opResolver;
s_opResolver.AddSoftmax();
s_opResolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());

tflite::MicroInterpreter interpreter(model, s_opResolver, tensorArena, arenaSize);
interpreter.AllocateTensors();
interpreter.Invoke();  // chạy suy luận - phần NEUTRON_GRAPH tự chuyển sang chạy trên NPU
```

**Không cần gọi hàm "khởi động NPU" thủ công nào cả** - `neutronInit()` (hàm
init phần cứng NPU thật sự, nằm trong driver) được **chính kernel
`NEUTRON_GRAPH` tự gọi bên trong**, ngay lần đầu tiên `Prepare()` (bước
chuẩn bị trước khi chạy) của nó được TFLM gọi tới. Đây là thiết kế "lazy
init" phổ biến - coprocessor tự lo phần khởi động của chính nó, code gọi
chỉ cần dùng API TFLM chuẩn như mọi model khác.

### Lượng tử hóa (quantization) int8 - vì sao công thức lại đơn giản bất ngờ

Input tensor model là `int8[1,64,64,3]`, với 2 tham số lượng tử hóa:
`scale=0.003922` (≈ 1/255), `zero_point=-128`. Công thức quantize chuẩn là:

```
gia_tri_luong_tu = round(gia_tri_thuc / scale) + zero_point
```

Vì `scale ≈ 1/255`, `1/scale ≈ 255` - tức là **1 đơn vị pixel [0..255] map
đúng thành 1 đơn vị lượng tử**. Kết hợp `zero_point = -128`, công thức rút
gọn lại chỉ còn: `gia_tri_luong_tu = pixel - 128` (dịch [0,255] xuống
[-128,127]). Không cần chia/nhân số thực gì cả lúc chạy - cực nhanh trên
MCU. Đây là kiểu quantization "đối xứng" phổ biến khi ảnh input được chuẩn
hóa kiểu Keras `rescale(1/127.5) - 1` trước khi train.

### Đo thời gian đúng cách - `ei_result.timing` không hoạt động

Cả 2 đường code AI đều dùng chung 1 cách đo thời gian: **bộ đếm chu kỳ DWT**
(Data Watchpoint and Trace) có sẵn trên mọi lõi ARM Cortex-M có debug unit:

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  // bật khối trace
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             // bật bộ đếm chu kỳ

uint32_t start = DWT->CYCCNT;
// ... chạy suy luận ...
uint32_t elapsedCycles = DWT->CYCCNT - start;
uint32_t elapsedUs = (uint64_t)elapsedCycles * 1000000ULL / SystemCoreClock;
```

Ban đầu định dùng `ei_result.timing.classification_us` (trường có sẵn trong
struct kết quả của Edge Impulse SDK) nhưng nó **luôn trả về 0** - vì lớp
"porting" clib của SDK (`ei_classifier_porting.cpp`) định nghĩa cứng
`ei_read_timer_us() { return 0; }` (không phải hàm `weak`, không override
được) - đây là hàm dành cho các nền tảng có sẵn hệ điều hành/thư viện thời
gian chuẩn, bare-metal MCU thuần không có gì để nó gọi tới nên SDK chọn trả
về 0 thay vì crash. Bài học: **đừng tin số liệu timing "có sẵn" từ 1 SDK/thư
viện tổng quát khi chạy trên nền tảng bare-metal lạ** - luôn kiểm chứng
bằng cách đo tay (DWT, hoặc timer phần cứng) trước khi dựa vào số đó.

### Kết quả đo được

| | CPU (CMSIS-NN) | NPU (Neutron) |
|---|---|---|
| Thời gian/lần | ~1.27 giây | ~3.3 mili-giây |
| Tỉ lệ | 1x | **~370-390x** |

---

## 6. USB: tại sao bỏ UVC streaming

Project ban đầu có thử stream video qua USB (UVC - USB Video Class, tức là
"webcam chuẩn" mà máy tính nhận ra ngay không cần driver riêng). Tính năng
này **đã bị bỏ**, không phải vì lỗi code mà vì **giới hạn phần cứng thật
sự** của board - đáng để hiểu rõ vì đây là kiểu ràng buộc hay gặp khi làm
việc với board eval giá rẻ.

### Vấn đề: điện áp DCDC xung đột

Chip MCXN947 có bộ điều chỉnh điện áp lõi (DCDC regulator) chạy ở nhiều
**mức điện áp khác nhau** tùy nhu cầu hiệu năng - mức thấp tiết kiệm điện,
mức cao cho phép chạy nhanh/dùng được nhiều ngoại vi tốc độ cao hơn:

- **Mid voltage (~1.0V)**: đủ cho SmartDMA chạy camera capture ổn định.
  Camera **chỉ hoạt động tin cậy ở mức này** (đã xác nhận qua nhiều lần
  test phần cứng thực tế).
- **Overdrive (~1.2V)**: mức cao hơn, **bắt buộc** để PLL của khối USB
  High-Speed PHY có thể khóa pha (lock) và hoạt động đúng.

Hai khối này **cần 2 mức điện áp khác nhau, không giao nhau** - nghĩa là
tại một thời điểm, chỉ có thể chạy được camera **hoặc** USB HS, không thể
cả hai cùng lúc.

### Vấn đề thứ hai: không có đường vòng phần cứng

Bản thân chip MCXN947 thực ra **có** một khối USB Full-Speed (FS) riêng,
không cần điện áp Overdrive - nếu board có nối chân USB FS ra 1 connector
vật lý, có thể dùng khối đó thay cho HS để tránh xung đột. Nhưng theo tài
liệu board FRDM-MCXN947 (UM12018, mục 2.3), **connector USB Type-C duy nhất
trên board được đấu cứng vào khối HS** - không có cách nào truy cập khối FS
mà không phải hàn lại phần cứng (rework board).

### Vì sao đây là giới hạn phần cứng, không phải bug phần mềm

Nếu chỉ là vấn đề timing/cấu hình phần mềm, có thể sửa bằng code. Nhưng ở
đây, **bản chất vật lý** (PLL của USB HS PHY cần điện áp X để khóa pha) kết
hợp **cách đấu dây cố định trên board** (connector chỉ nối tới khối cần
điện áp đó) tạo thành 1 giới hạn không thể vượt qua bằng phần mềm - dù có
viết driver hoàn hảo đến đâu, USB HS vẫn không thể chạy cùng lúc với
camera trên chính board này.

### Giải pháp workaround đã thử (và tại sao vẫn coi là "abandoned")

Đã xây dựng được 1 cơ chế **chia thời gian** (time-multiplexing): định kỳ
chuyển giữa 2 trạng thái - "Mid voltage, bắt vài khung hình camera mới" rồi
"chuyển Overdrive, gửi khung hình cũ đó qua USB trong vài giây" rồi lặp lại.
Cách này **có chạy được thật** (xác nhận qua `lsusb`, `dmesg`, và 1 phiên
`gst-launch-1.0` capture liên tục ổn định) - nhưng cho ra hình ảnh giật,
low-fps thực sự (không phải video mượt như webcam thật), và có độ phức tạp
code cao (2 trạng thái điện áp, đồng bộ hóa thời điểm chuyển). Vì mục tiêu
chính của project (drowsy detection) không cần xem lại video qua USB - LCD
tại chỗ đã đủ - tính năng này bị gác lại, ưu tiên quay về đường LCD đơn
giản hơn nhiều.

**Bài học chung:** khi 2 tính năng cùng cần "tài nguyên chia sẻ độc quyền"
của chip (ở đây là mức điện áp lõi) và không có đường vòng phần cứng, cách
duy nhất là time-multiplexing (chia thời gian dùng) - chấp nhận đánh đổi độ
mượt/độ trễ, hoặc từ bỏ 1 trong 2 tính năng. Luôn kiểm tra tài liệu phần
cứng của board (không chỉ datasheet chip) trước khi giả định "chip hỗ trợ
X nên board cũng dùng X được" - board có thể chỉ đấu dây tới 1 phần nhỏ
trong những gì chip hỗ trợ.

---

## 7. Bài học tổng quát

Vài nguyên tắc rút ra được áp dụng lặp lại nhiều lần xuyên suốt project,
đáng nhớ khi làm việc với chip có nhiều coprocessor (SmartDMA, NPU...):

1. **"RAM/vùng nhớ trông như trống" chưa chắc là trống** - kiểm tra xem có
   coprocessor nào cắm cố định vào đó trước khi tái sử dụng (mục 1, 2).
2. **Đừng đoán bit trong thanh ghi phần cứng** - luôn tra đúng header CMSIS
   của chip, và dùng disassembly để xác nhận thay vì suy luận từ triệu
   chứng bên ngoài (mục 4).
3. **Số liệu "có sẵn" từ SDK/thư viện tổng quát có thể không hoạt động trên
   nền tảng bare-metal lạ** - luôn tự kiểm chứng bằng phép đo độc lập trước
   khi tin tưởng (mục 5, phần đo thời gian).
4. **Coprocessor tăng tốc (NPU, DSP...) thường cần dữ liệu được "biên dịch"
   riêng cho nó**, không đơn giản là 1 cờ bật/tắt trên model chuẩn - cần
   công cụ convert riêng của nhà sản xuất (mục 5).
5. **Giới hạn tài nguyên chia sẻ (điện áp, băng thông bus...) là giới hạn
   vật lý của board, không sửa được bằng code** - nhận diện sớm để không
   tốn thời gian debug sai hướng (mục 6).
6. **Khi coprocessor tự lo phần khởi động của chính nó** (như `neutronInit()`
   được gọi ngầm bên trong kernel), không cần và không nên tự gọi lại thủ
   công - đọc kỹ code/tài liệu driver trước khi thêm bước init thừa.

Xem [WORKLOG.md](WORKLOG.md) để biết chi tiết từng bug cụ thể (log thực tế,
số liệu đo đạc, các lần thử-sai) nếu cần đào sâu hơn nội dung tổng hợp ở
trên.
