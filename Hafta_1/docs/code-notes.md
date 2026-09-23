# Kod notları

## 1. Modül haritası

**Firmware (`firmware/Cubeide_Manuel/Core`)**

| Dosya | Sorumluluk |
|---|---|
| `app_config.h` | Tüm sabitler: pinler, çerçeve boyutları, deadline, debounce, kuyruk derinlikleri, öncelikler, yığınlar |
| `app_timestamp.c/h` | DWT açma (LAR dahil), cycle → µs |
| `app_ring_buffer.c/h` | 256 kayıtlık olay tamponu, `(ring_index, event_id)` doğrulamalı yazma, sayaçlar, dump iteratörü |
| `app_protocol.c/h` | CRC-8, 64 baytlık çerçeve kodlayıcıları (BOOT/TEL/BTN/REC/STAT/DUMP_END/ERR), komut ayrıştırıcı |
| `app_button.c/h` | EXTI ISR: debounce, t₀, olay kimliği, ButtonQueue |
| `app_cmd_rx.c/h` | USART1 RX IT: satır biriktirme, CmdQueue, hata sonrası yeniden kollama |
| `app_tasks.c` | UartTxTask (TX + komut + dump), ButtonTask, TelemetryTask |
| `app_rtos.c/h` | Paylaşılan RTOS nesneleri ve `App_RTOS_Init()` |
| `app_hal_callbacks.c` | HAL weak callback'lerinin override'ları (t₄ burada) |
| `app_hw.h` | CubeMX'in `main.c`'de tanımladığı `huart1` için `extern` bildirim (`CoupleFile=false`) |
| `main.c` (CubeMX) | Yalnızca boş USER CODE blokları dolduruldu: Includes → `app_rtos.h`, RTOS_THREADS → `App_RTOS_Init()` |
| `freertos.c` (CubeMX) | Değiştirilmedi; FreeRTOS hook'ları CubeMX'in tanımları |

**Arayüz (`interface`)**

| Dosya | Sorumluluk |
|---|---|
| `protocol.py` | CRC-8, çerçeve ayrıştırma, `FrameReader` (resync), komut üretimi |
| `serial_worker.py` | Seri port okuyan QThread; ham çerçeveleri iletir |
| `session.py` | Oturum: ham çerçeveler + çözümlenmiş durum; `.json` kaydet/aç, `S<n>.csv` aç |
| `demo.py` | Sentetik demo: firmware zamanlama simülasyonu (kartta ölçülen temel sürelerle) |
| `theme.py`, `widgets.py` | Koyu tema, KPI kutusu, aşama çubukları |
| `data_model.py` | `ScenarioRun`, CSV yazımı, özet satırı, `summary.csv` upsert |
| `plotting.py` | İki zorunlu grafik (diskteki CSV'den); CLI ile yeniden üretim |
| `main_window.py` | Canlı ölçüm paneli (PyQt6) |

## 2. Kritik tasarım kararları

- **t₄ ISR'da:** UartTxTask en düşük öncelikli; t₄'ü görev uyanınca almak, araya giren
  TelemetryTask/ButtonTask sürelerini (CPU yükü dahil) S4'e eklerdi.
- **BTN yanıtında t₂ yok:** t₂ "yanıt üretildikten sonra, kuyruğa bırakmadan hemen önce"
  tanımlı; yanıtın kendisi t₂'yi taşıyamaz (t₄ ile aynı nedensellik). Tam kayıt dump ile gelir.
- **Kayıt yazımları ölçüm zincirinin dışında:** t₂ ve t₃ yerel değişkende tutulur, RAM
  kaydına ilgili çağrı (put / DMA başlatma) yapıldıktan sonra yazılır.
- **Komut ayrıştırma görevde:** RX ISR yalnızca bayt biriktirir. ISR kısa kalır, newlib
  (strtoul vb.) ISR'dan çağrılmaz.
- **TelemetryTask yeniden hizalama:** Yük ≥ periyot ise kaçırılan periyotlar telafi edilmez;
  görev yine bir periyot uyur. Aksi halde en yüksek öncelikli görev hiç bloklanmaz ve
  STOP'u işleyecek UartTxTask dahil herkes aç kalır.
- **CubeMX kodu önceliklidir:** CubeMX'in ürettiği kod (varsayılan gövdeler dahil) değiştirilmez;
  uygulama ona uyar. Çakışan sembol tanımlanmaz, yalnızca boş USER CODE bloklarına ekleme yapılır.
- **Debounce:** Kenar yalnızca önceki *herhangi bir* kenardan ≥ 30 ms sessizlik sonra
  gelirse ve pin basılı seviyedeyse kabul edilir; EXTI her iki kenarda. Değer
  `APP_DEBOUNCE_MS` (`app_config.h`) ile değiştirilir.

## 3. CPU yükü kalibrasyonu

Her TEL, o periyotta yük döngüsünün DWT ile ölçülen süresini (`load_us`) taşır; arayüz
bunu periyoda oranıyla gösterir.

1. S3'ü periyot 50 ms, yük 10 000 ile başlatın, birkaç TEL bekleyin, `load_us` değerini not edin.
2. Döngü doğrusal olduğu için: `iterasyon_başına_µs = load_us / 10000`.
3. İstenen oran için: `load_iter = hedef_µs / iterasyon_başına_µs`
   (örn. 50 ms periyotta %30 → 15 000 µs).
4. Sonuçları aşağıya yazın.

| Ölçüm | Değer |
|---|---|
| iterasyon başına süre | _ölçülecek_ |
| S3/S4 "orta" `load_iter` | _ölçülecek_ |
| S5 "ağır" `load_iter` | _ölçülecek_ |

## 4. Host doğrulaması (bu ortamda yapılanlar)

Kart olmadan yapılabilen her şey otomatik test edildi:

| Test | Kapsam | Sonuç |
|---|---|---|
| Derleme | 9 firmware kaynağı, HAL/CMSIS-RTOS2 imzalarını taklit eden başlıklarla, `gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes` | hatasız |
| C birim testleri | CRC kontrol değeri 0xF4; tampon taşması (300 olay → 44 taşma, dump sırası 45..300); yanlış olay kimliğiyle yazmanın reddi; deadline sınırı (20.000 ms OK, 20.001 ve 20.9 ms LATE); DWT taşması üzerinden ölçüm; kritik bölge dengesi; 62 baytı aşan mesajın ERR'e dönüşmesi | hepsi geçti |
| C ↔ Python | Python'un ürettiği 6 komutu C ayrıştırdı; bozuk CRC ve 8 hatalı komut reddedildi; C'nin ürettiği 11 çerçeveyi Python doğru çözdü (eksik zaman damgaları, taşma dahil) | hepsi geçti |
| Arayüz (offscreen) | Kart yolu (sahte seri port): BOOT, CFG+START, ms→iterasyon, ayar doğrulaması, canlı durum/olay tablosu, STOP→DUMP, tekrar gelen REC, KPI, CSV/özet/PNG/ham oturum otomatik kaydı, kalibrasyon; oturum kaydet/aç aynı özeti verir; DUMP yeniden deneme; sentetik demo `measurements/`'a yazmaz; gerçek S0/S1 ölçümlerini açma (33 kontrol) | hepsi geçti |
| Çerçeve kodlayıcı | Python `encode_message`, C firmware'in ürettiği 10 çerçeveyle bayt bayt aynı | geçti |
| ARM derlemesi | Uygulama + FW_F7 1.17.4'ün gerçek HAL, CMSIS, FreeRTOS 10.2.0, CMSIS-RTOS2 kaynakları + CubeMX başlatma kodunun eşdeğeri; CubeIDE 1.16.0'ın GCC 12.3'ü, `-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard`, uygulamada `-Wextra -Wshadow -Werror` | uyarısız link; flash 45 KB, RAM 39 KB |
| Gerçek proje | CubeMX'in ürettiği UART_RTOS_ODEV1, kendi makefile'ı ile | uyarısız link; flash 47.7 KB, RAM 39 KB |
| ELF denetimi | HAL callback'leri uygulamadan, hook'lar CubeMX'in `freertos.c`'sinden, `SysTick_Handler` FreeRTOS portundan bağlanıyor; `Timestamp_Init` içinde `0xC5ACCE55` var | doğrulandı |
| Yığın | `-fstack-usage`: en derin zincir ≈ 1.1 KB (UartTxTask dump), ≈ 0.8 KB (TelemetryTask), ≈ 0.7 KB (ButtonTask) | yığınlar 1536 / 1024 / 1536 B |

Testler sırasında bulunan ve düzeltilen hata: Windows'ta `glob("S*.csv")` büyük/küçük
harf duyarsız olduğu için `summary.csv`'yi de yakalıyordu.

**Kart üzerinde:** kullanıcı 2026-09-22'de S0 (200 ms, yük yok, 55 olay) ve S1 (10 ms, 1600 iter,
50 olay) ölçümlerini aldı; tüm olaylar OK, t₁−t₀ ≈ 16 µs, t₂−t₁ ≈ 82 µs, t₄−t₃ ≈ 5563 µs
(teorik 5556 µs). Sentetik demonun temel süreleri bu ölçümden alındı.
