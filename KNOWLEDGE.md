# KNOWLEDGE.md — Kiến thức nền để hiểu project này

File này **không nói về project cụ thể này đã làm gì** (cái đó xem
[ARCHITECTURE.md](ARCHITECTURE.md)), mà giải thích **các khái niệm kỹ
thuật đứng sau nó** — những thứ nếu chưa biết thì đọc code sẽ không hiểu
tại sao lại viết như vậy. Viết theo kiểu giảng lại từ đầu, không cần biết
trước gì về vi điều khiển.

Mục lục:
1. [Camera số "nhìn thấy" và gửi ảnh đi như thế nào](#1-camera-số-nhìn-thấy-và-gửi-ảnh-đi-như-thế-nào)
2. [Tại sao CPU không tự chụp ảnh được — khái niệm "coprocessor"](#2-tại-sao-cpu-không-tự-chụp-ảnh-được--khái-niệm-coprocessor)
3. [AI chạy trên một con chip nhỏ xíu như thế nào](#3-ai-chạy-trên-một-con-chip-nhỏ-xíu-như-thế-nào)
4. [NPU — phần cứng chuyên chạy AI, khác CPU ở đâu](#4-npu--phần-cứng-chuyên-chạy-ai-khác-cpu-ở-đâu)
5. [Bộ nhớ RAM trong chip nhúng không "phẳng" như bạn tưởng](#5-bộ-nhớ-ram-trong-chip-nhúng-không-phẳng-như-bạn-tưởng)
6. [Nạp chương trình vào chip: SWD/JTAG và "Debug Mailbox"](#6-nạp-chương-trình-vào-chip-swdjtag-và-debug-mailbox)
7. [Điện áp lõi chip: vì sao camera và USB "đánh nhau"](#7-điện-áp-lõi-chip-vì-sao-camera-và-usb-đánh-nhau)
8. [Bảng thuật ngữ nhanh](#8-bảng-thuật-ngữ-nhanh)

---

## 1. Camera số "nhìn thấy" và gửi ảnh đi như thế nào

### Ảnh số thực chất là một lưới số

Một tấm ảnh 320×240 là một lưới 320 cột × 240 hàng ô vuông (pixel), mỗi ô
là 1-3 byte số (độ sáng/màu). Camera không "gửi cả tấm ảnh" một lần — nó
gửi **từng byte một, theo một nhịp đồng hồ**, giống như đọc từng ký tự
trong một cuốn sách theo đúng thứ tự trái-qua-phải, trên-xuống-dưới.

Ở 320×240, 30 khung hình/giây: 320 × 240 × 30 ≈ **2,3 triệu byte mỗi
giây** phải được "hứng" đúng lúc — không hứng kịp là mất pixel, ảnh bị
lỗi/nhòe.

### Hai cách camera "nói chuyện" với chip: song song (DVP) hay nối tiếp (MIPI-CSI)

Camera trong project này (OV7670) dùng **DVP** (Digital Video Port) — một
kiểu giao tiếp **song song**, nghĩa là nó có **8 dây dữ liệu riêng** (gọi
là D0..D7), mỗi dây mang 1 bit, nên **1 byte pixel đi trong đúng 1 nhịp
đồng hồ**. Ngoài 8 dây dữ liệu, DVP còn có 3 dây "tín hiệu nhịp":

- **PCLK** (Pixel Clock) — do chính camera phát ra, giống tiếng "tách" của
  máy đếm: mỗi lần tách, có nghĩa là "dữ liệu trên D0-D7 đang hợp lệ, đọc
  ngay bây giờ".
- **HREF** — bật lên trong lúc camera đang gửi *một hàng ngang* của ảnh.
- **VSYNC** — đánh dấu điểm bắt đầu/kết thúc *một khung hình đầy đủ*.

Camera đời mới hơn thường dùng **MIPI-CSI**, kiểu **nối tiếp** — chỉ 1-2
dây tốc độ rất cao thay vì 8 dây, dữ liệu được "dồn" (serialize) lại rồi
"tháo" (deserialize) ra ở đầu nhận. Giống như so sánh giữa việc chở 8
người bằng 8 xe máy chạy song song (DVP) và chở 8 người bằng 1 xe khách
chạy rất nhanh (MIPI-CSI) — MIPI-CSI cần thêm mạch điện tử để "gói/tháo
gói", nhưng cần ít dây hơn hẳn nên phù hợp cho camera nhỏ gọn hiện đại.

### Có một kênh thứ ba, tách biệt hoàn toàn: kênh điều khiển (SCCB)

Ngoài luồng ảnh, camera còn có 2 dây riêng (SIOC/SIOD) chạy giao thức
**SCCB** — gần giống hệt **I2C** (một chuẩn giao tiếp 2 dây rất phổ biến
để "hỏi/ra lệnh" cho các linh kiện điện tử nhỏ). Kênh này **không truyền
ảnh** — nó chỉ dùng để chip "ra lệnh" cho camera: đặt độ phân giải, độ
sáng, lật ảnh, v.v. Hình dung: HREF/VSYNC/PCLK/D0-D7 là "băng chuyền ảnh",
còn SCCB là "bộ đàm" nói chuyện riêng với camera để cấu hình nó.

---

## 2. Tại sao CPU không tự chụp ảnh được — khái niệm "coprocessor"

### CPU là "người quản lý bận rộn", không phải "công nhân dây chuyền"

CPU (Cortex-M33 trong chip này) chạy chương trình chính — logic, AI, hiển
thị màn hình... Nếu bắt CPU tự đọc từng bit trên D0-D7 đúng lúc PCLK
"tách" (làm bằng phần mềm, gọi là *bit-banging*), CPU sẽ phải bỏ hoàn
toàn mọi việc khác để canh me từng cạnh xung nhịp, ~2,3 triệu lần mỗi
giây — không còn thời gian làm gì khác (chạy AI, vẽ màn hình...). Đây là
vấn đề **thời gian thực cứng** (hard real-time): trễ một nhịp là mất dữ
liệu vĩnh viễn, không "chờ rồi làm bù" được.

### Giải pháp: thuê một "công nhân chuyên trách" — coprocessor

Nhiều chip vi điều khiển hiện đại có thêm những **bộ xử lý phụ nhỏ, độc
lập** bên trong, mỗi bộ chuyên làm đúng một việc lặp đi lặp lại tốc độ
cao, chạy **song song** với CPU chính mà không cần CPU can thiệp từng
bước. Chip trong project này có **SmartDMA** — khác với "DMA thường" (chỉ
copy khối bộ nhớ A→B theo lệnh có sẵn), SmartDMA là một **bộ xử lý có thể
nạp firmware/vi mã (microcode) riêng**, tự hiểu được nhịp PCLK/HREF/VSYNC
và tự ráp từng byte thành khung hình hoàn chỉnh trong bộ nhớ, hoàn toàn
độc lập với CPU.

Ví von: nếu CPU là quản lý nhà hàng (lo thực đơn, order, tính tiền...) thì
SmartDMA là một máy pha cà phê tự động chuyên dụng — cắm nguyên liệu vào,
nó tự làm đúng quy trình đó liên tục, quản lý chỉ cần ra lệnh "bắt đầu" và
nhận thông báo "xong rồi" (ngắt/interrupt), không cần đứng canh từng
bước.

Đây là **một mẫu hình rất phổ biến** trong chip nhúng hiện đại: phần nào
đòi hỏi thời gian thực khắt khe (bắt đúng từng xung nhịp) thì giao cho một
lõi phần cứng chuyên dụng; phần nào là logic/quyết định thì để CPU chính
lo. Bạn sẽ gặp lại đúng mẫu hình này ở phần AI (mục 4).

---

## 3. AI chạy trên một con chip nhỏ xíu như thế nào

### "Model AI" chỉ là một đống phép nhân-cộng rất lớn

Một mạng nơ-ron (neural network) sau khi huấn luyện xong, về bản chất là
**một danh sách các con số cố định** (gọi là *trọng số* — weights) và một
**thứ tự các phép toán** (chủ yếu là nhân ma trận + cộng) áp lên ảnh đầu
vào để ra một kết quả (ví dụ: "có mặt người hay không, ở đâu"). "Chạy AI"
(gọi là **inference**) nghĩa là thực hiện đúng chuỗi phép toán đó trên dữ
liệu mới — không "học" gì thêm lúc chạy, việc học (training) đã làm xong
từ trước, trên máy tính mạnh, không phải trên chip nhỏ này.

### Vấn đề: số thực (float) rất "đắt" với một MCU nhỏ

Máy tính thường tính với số thực dấu phẩy động (float, ví dụ 0.7391...).
Nhưng một vi điều khiển nhỏ (MCU) không có phần cứng tính float nhanh như
máy tính/điện thoại — tính float trên MCU chậm và tốn năng lượng. Giải
pháp gọi là **lượng tử hóa (quantization)**: đổi hết các số thực đó thành
**số nguyên 8-bit** (int8, chỉ từ -128 đến 127) trước khi nạp vào chip,
theo một công thức quy đổi cố định:

```
giá_trị_lượng_tử = round(giá_trị_thực / scale) + zero_point
```

`scale` và `zero_point` là hai con số cố định được tính sẵn lúc huấn
luyện, đi kèm theo model. Với ảnh (pixel gốc 0..255), model trong project
này có `scale ≈ 1/255` và `zero_point = -128` — nghĩa là công thức trên
rút gọn lại chỉ còn **`giá_trị_lượng_tử = pixel - 128`** (chỉ là phép trừ,
không cần nhân/chia gì cả) — cực kỳ rẻ để tính trên một MCU không có bộ
tính float mạnh.

Đổi lại, số nguyên 8-bit có ít "mức" hơn số thực rất nhiều, nên độ chính
xác của model giảm đi một chút so với bản gốc — đây là cái giá phải trả
để model chạy vừa trên một con chip vài trăm KB RAM thay vì cần cả GB như
trên máy tính.

### FOMO: một kiểu model được "cắt gọt" riêng cho MCU

Các model nhận diện vật thể "đầy đủ" (như YOLO) thường vẽ được khung
(bounding box) rất chính xác nhưng nặng, không chạy nổi trên MCU. **FOMO**
(Faster Objects, More Objects — một kiểu kiến trúc do Edge Impulse tạo ra)
là bản rút gọn: thay vì "vẽ khung" chính xác, nó chia ảnh thành **một
lưới ô vuông nhỏ** và chỉ trả lời "ô này có tâm vật thể hay không" cho
từng ô — rẻ hơn nhiều lần về tính toán, đổi lại độ chính xác vị trí thô
hơn (theo kích thước ô lưới, không phải theo từng pixel).

---

## 4. NPU — phần cứng chuyên chạy AI, khác CPU ở đâu

### Vì sao CPU "chạy được" AI nhưng vẫn cần thêm phần cứng riêng

CPU có thể chạy AI bằng phần mềm thuần (ở đây gọi là đường "CPU +
CMSIS-NN" — CMSIS-NN là một thư viện được NXP/ARM tối ưu sẵn cho các phép
nhân ma trận trên CPU dòng Cortex-M). Nhưng phần lớn thời gian tính AI là
**hàng triệu phép nhân-cộng lặp đi lặp lại giống hệt nhau** (nhân ma trận
cho từng lớp conv/depthwise-conv trong model) — CPU (dù có CMSIS-NN hỗ
trợ) vẫn xử lý các phép này **tuần tự từng bước một**.

### NPU: nhiều "bộ nhân" chạy song song, chỉ để làm đúng việc đó

**NPU** (Neural Processing Unit — ở đây là "Neutron", coprocessor AI của
NXP) là một mạch phần cứng thiết kế **chỉ để** làm phép nhân-cộng ma trận,
nhưng làm **hàng trăm/hàng nghìn phép cùng lúc** thay vì từng phép một,
nhờ có rất nhiều mạch nhân nhỏ chạy song song. Đây chính là mẫu hình
coprocessor đã nói ở mục 2 — CPU giao việc, NPU tự chạy độc lập, gửi kết
quả lại. Trên model từng đo trong project này, chênh lệch tốc độ đo được
trên phần cứng thật là:

| | CPU (CMSIS-NN) | NPU (Neutron) |
|---|---|---|
| Thời gian mỗi lần suy luận | ~1,27 giây | ~3,3 mili-giây |
| Tốc độ tương đối | 1x | **nhanh hơn ~370-390 lần** |

### NPU không tự nhiên "hiểu" mọi model — cần một bước chuyển đổi riêng

Đây là điểm khác biệt lớn nhất so với CPU: CPU (qua CMSIS-NN) chạy được
gần như mọi model TFLite int8 chuẩn ngay lập tức. NPU thì không — nó chỉ
hiểu một tập lệnh/microcode riêng của chính nó. Cần một công cụ chuyển đổi
(ở đây là `neutron_converter` của NXP) quét qua model, tìm các lớp mà NPU
hỗ trợ (conv, pool, add...), rồi **gộp cả cụm lớp đó lại thành một "khối
lệnh" duy nhất** đã biên dịch sẵn cho NPU. Những lớp NPU không hỗ trợ vẫn
chạy bình thường trên CPU như cũ — model không cần "toàn bộ" chạy trên NPU
mới có lợi, chỉ cần phần nặng nhất (thường là >90% khối lượng tính toán)
được gộp là đã nhanh hơn rất nhiều.

---

## 5. Bộ nhớ RAM trong chip nhúng không "phẳng" như bạn tưởng

### Trên máy tính, "RAM" là một khối liền; trên chip nhúng thì không

Khi lập trình trên máy tính/điện thoại, bạn hầu như không cần biết RAM
"nằm ở đâu" — hệ điều hành lo hết. Nhưng bên trong một con chip vi điều
khiển, RAM thực ra được chia thành **nhiều bank vật lý riêng biệt** (ví
dụ: `m_data`, `m_sramx`...), mỗi bank có thể:

- Được nối vào các đường bus khác nhau (nên tốc độ truy cập khác nhau),
- Bị **một coprocessor khác "chiếm dụng ngầm"** để lưu firmware/trạng thái
  riêng của nó, mà **trình biên dịch/linker không hề biết** — vì
  coprocessor đó nạp dữ liệu vào bank đó lúc chương trình đang chạy (qua
  một lệnh gọi hàm), không phải lúc biên dịch.

Hệ quả: một bank RAM "trông có vẻ trống" khi bạn nhìn vào bản đồ bộ nhớ do
trình biên dịch tạo ra (linker map), **không có nghĩa là nó thực sự
trống** lúc chương trình chạy. Đây là một cái bẫy rất dễ mắc phải: nhìn
linker map thấy "chưa ai dùng bank này", tưởng an toàn để nhét dữ liệu
của mình vào, nhưng thực ra một coprocessor khác đang âm thầm dùng nó.

### Không phải mọi bank RAM đều "được cấp điện"

Có một cấp bẫy sâu hơn nữa: trên chip nhiều lõi CPU (đa nhân), nhà sản
xuất thường chia RAM theo **vùng cấp điện riêng** (power domain) cho từng
lõi, để tiết kiệm điện — lõi nào không dùng thì cắt điện luôn cả vùng RAM
gắn với nó. Nếu chương trình chỉ chạy 1 lõi và **không bao giờ khởi động
lõi thứ hai**, thì vùng RAM dành riêng cho lõi đó có thể **chưa từng được
cấp điện** — không phải "đang được dùng bởi ai đó" như trường hợp trên,
mà là **về mặt vật lý mạch điện chưa bật**. Truy cập vào đó không gây "dữ
liệu sai" — nó gây lỗi bus (bus fault), chương trình treo cứng ngay lập
tức.

Bài học chung: **"linker map thấy trống" không đồng nghĩa với "an toàn để
dùng"** — cần biết rõ có coprocessor/lõi nào khác đang (hoặc có thể) đụng
vào vùng nhớ đó không, trước khi tái sử dụng nó.

---

## 6. Nạp chương trình vào chip: SWD/JTAG và "Debug Mailbox"

### Làm sao máy tính "nói chuyện" được với một con chip chưa chạy gì cả

Khi bạn nạp code vào vi điều khiển, máy tính không dùng USB/mạng thông
thường — nó dùng một giao thức phần cứng cấp thấp gọi là **SWD** (Serial
Wire Debug, chỉ 2 dây) hoặc **JTAG** (nhiều dây hơn, cũ hơn), đi qua một
mạch nhỏ trung gian gọi là **debug probe** (ở đây là MCU-Link, gắn ngay
trên board). Đây là kênh **cấp thấp nhất có thể** — nó hoạt động được kể
cả khi chip chưa hề chạy chương trình nào, dùng để: nạp firmware vào
flash, đọc/ghi thanh ghi, tạm dừng CPU để debug từng dòng lệnh.

### Vì sao đôi khi công cụ nạp chip "chuẩn" vẫn không hoạt động

Công cụ nạp chip phổ biến (`pyOCD`) hoạt động dựa trên một **gói mô tả
chip** (CMSIS-Pack) do nhà sản xuất cung cấp — file này mô tả các bước cụ
thể ("debug sequence") cần làm để kết nối đúng cách với từng dòng chip cụ
thể (mỗi hãng, mỗi dòng chip có thể khác nhau). Nếu gói mô tả này (hoặc
cách `pyOCD` thực thi nó) không khớp hoàn hảo với đúng phiên bản
probe/chip đang dùng, bước kết nối có thể thất bại — dù kết nối vật lý
(dây SWD) hoàn toàn ổn.

Trong trường hợp đó, có một đường vòng: nhiều chip có sẵn một cơ chế điều
khiển thấp hơn nữa, gọi là **Debug Mailbox** — về cơ bản là một "hộp thư"
phần cứng nhỏ mà bạn gửi lệnh vào (mở khóa truy cập, xóa flash...) mà
không cần đi qua toàn bộ quy trình debug sequence phức tạp ở trên. Công
cụ `nxpdebugmbox` (từ chính NXP) nói chuyện trực tiếp qua kênh này, và có
sẵn cơ chế tự thử lại khi gặp lỗi kết nối — cái mà `pyOCD` không có sẵn
cho riêng dòng chip này.

Bài học chung: khi công cụ nạp chip "chuẩn" báo lỗi ngay từ bước kết nối
(chưa tới bước ghi flash), đừng chỉ thử đổi tốc độ/cáp/cổng USB — hãy tìm
xem nhà sản xuất chip có công cụ debug cấp thấp hơn riêng của họ không.

---

## 7. Điện áp lõi chip: vì sao camera và USB "đánh nhau"

### Chip không chạy ở một điện áp cố định duy nhất

Nhiều chip hiện đại có một mạch điều chỉnh điện áp lõi bên trong gọi là
**DCDC regulator** (hay buck converter) — nó có thể **thay đổi điện áp
cấp cho lõi CPU/các khối chức năng**, tùy theo việc chip đang cần tốc độ
cao (điện áp cao hơn) hay tiết kiệm điện (điện áp thấp hơn). Đây là kỹ
thuật rất phổ biến để cân bằng hiệu năng/năng lượng.

### Vấn đề: hai thiết bị cần hai mức điện áp khác nhau, không thể cùng lúc

Trong project này, camera (qua SmartDMA) chạy ổn định nhất ở điện áp
**trung bình (~1.0V)**, còn cổng **USB tốc độ cao (High-Speed)** cần điện
áp **cao hơn (~1.2V, gọi là chế độ Overdrive)** thì mạch PLL của nó mới
**khóa pha** được (PLL là mạch tạo xung nhịp cực chính xác cần thiết để
truyền dữ liệu tốc độ cao — nếu điện áp không đủ, PLL không "bắt" được
tần số đúng, USB không hoạt động được).

Vì đây là **cùng một mạch DCDC**, chip **chỉ có thể ở một mức điện áp tại
một thời điểm** — không thể vừa chạy camera ổn định vừa chạy USB
High-Speed ổn định cùng lúc. Đây khác hẳn với các lỗi phần mềm thông
thường: dù bạn viết driver camera và driver USB "đúng" tuyệt đối, hai cái
vẫn không thể chạy đồng thời, vì **giới hạn nằm ở phần cứng vật lý** (một
mạch điều áp, hai yêu cầu điện áp xung đột), không nằm ở logic code.

Cách duy nhất để "né" giới hạn này (nếu thực sự cần cả hai) là **luân
phiên theo thời gian** — chuyển qua chuyển lại giữa hai mức điện áp, mỗi
lần chỉ chạy một thứ trong một khoảng ngắn — đổi lại là cả hai đều chạy
"chập chờn" chứ không mượt liên tục.

---

## 8. Bảng thuật ngữ nhanh

| Thuật ngữ | Giải thích ngắn |
|---|---|
| **DVP** | Giao thức camera song song, 8 dây dữ liệu + 3 dây nhịp (PCLK/HREF/VSYNC). |
| **MIPI-CSI** | Giao thức camera nối tiếp, ít dây hơn DVP, phổ biến trên camera đời mới. |
| **SCCB** | Kênh điều khiển 2 dây của camera (giống I2C), dùng để cấu hình, không truyền ảnh. |
| **Coprocessor** | Bộ xử lý phụ chuyên một việc, chạy song song với CPU chính, không cần CPU can thiệp từng bước. |
| **SmartDMA** | Coprocessor của NXP, nạp được firmware riêng, ở đây dùng để tự "hứng" khung hình camera. |
| **Hard real-time** | Việc phải xử lý đúng hạn tuyệt đối — trễ là mất dữ liệu, không "làm bù" được. |
| **Inference** | Bước "chạy" một model AI đã huấn luyện xong trên dữ liệu mới, không học thêm gì. |
| **Quantization** | Đổi số thực (float) thành số nguyên nhỏ (int8) để tính nhanh/rẻ hơn trên phần cứng yếu. |
| **scale / zero_point** | Hai hằng số dùng trong công thức quy đổi số thực ↔ số nguyên lượng tử hóa. |
| **FOMO** | Kiểu model nhận diện vật thể rút gọn cho MCU — chia ảnh thành lưới ô, đoán "có tâm vật thể" theo ô. |
| **NPU** | Phần cứng chuyên chạy phép nhân-cộng ma trận của AI, song song hàng loạt thay vì tuần tự như CPU. |
| **TFLite Micro** | Runtime (bộ máy thực thi) chạy model AI dạng `.tflite` trên vi điều khiển, không cần hệ điều hành. |
| **Tensor arena** | Vùng RAM cấp phát sẵn để model AI dùng làm bộ nhớ tạm khi tính toán. |
| **RAM bank / power domain** | RAM trong chip nhúng chia thành nhiều vùng vật lý riêng, có vùng có thể chưa được cấp điện. |
| **SWD / JTAG** | Giao thức phần cứng cấp thấp để máy tính nạp code/debug trực tiếp vào chip. |
| **Debug probe** | Mạch trung gian (ví dụ MCU-Link) nối máy tính với chip qua SWD/JTAG. |
| **CMSIS-Pack / debug sequence** | Gói mô tả từng bước kết nối debug riêng cho từng dòng chip, do hãng chip cung cấp. |
| **Debug Mailbox** | Cơ chế điều khiển chip cấp thấp hơn cả SWD thường, dùng để mở khóa/xóa chip khi debug sequence chuẩn thất bại. |
| **DCDC regulator** | Mạch điều chỉnh điện áp lõi chip, có thể thay đổi mức điện áp tùy nhu cầu tốc độ/tiết kiệm điện. |
| **PLL** | Mạch tạo xung nhịp chính xác cần thiết cho truyền dữ liệu tốc độ cao (như USB High-Speed). |
</content>
