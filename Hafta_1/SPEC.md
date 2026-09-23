# SPEC — Yük Altında Buton Yanıt Süresi Analizi (Hafta 01)

**Durum:** v0.3 — denetimden geçirilmiş, uygulamayla birebir tutarlı sürüm.
**Platform:** STM32F746G-DISCO (C01) · FreeRTOS (CMSIS-RTOS v2 API) · STM32CubeMX + STM32CubeIDE · PC: Python (PySerial + PyQt6 + matplotlib + pandas).

## 0. Değişiklik geçmişi

| Sürüm | Değişiklik |
|---|---|
| v0.1 | İlk taslak: mimari, t₀–t₄, protokol, hata tablosu. |
| v0.2 | Kullanıcı kararları: IWDG yok; 1 bayt CRC-8; debounce 30 ms; STM32CubeIDE; CMSIS-RTOS v2. |
| v0.3 | Satır satır denetim. Düzeltilen ölçüm hataları: t₄ artık TC ISR'ında alınıyor (önce en düşük öncelikli görevde alınıyordu → S4 şişiyordu); S2 artık gerçekten yanıt üretimini ölçüyor (önce yanıt t₂'den sonra üretiliyordu → S2≈0); deadline µs ile karşılaştırılıyor (önce ms'ye kesiliyordu → 20.9 ms "OK" sayılıyordu); ölçülmemiş zaman damgaları `-` (önce 0 → DROP olayları grafikte R=0 görünüyordu). Kart kaynaklı düzeltmeler: Cortex-M7 DWT `LAR` kilidi, D-Cache/DMA tutarlılığı, CubeIDE 2.x'te CubeMX'in ayrı araç olması. Diğer tüm düzeltmeler §12'de. Protokolde: BTN'den t₂ çıktı, BOOT/ERR mesajları eklendi, STAT'a raporlanmayan sayaçlar eklendi, komut CRC örnekleri gerçek değerlerle değiştirildi. |

---

## 1. Amaç ve kapsam

Butona basıldığında orta öncelikli görevin ürettiği "butona basıldı" yanıtının UART'tan tamamen çıkma süresini **R = t₄ − t₀** ölçmek; telemetri hızı ve yapay CPU yükü değiştirildiğinde gecikmenin **hangi aşamada** (ISR→görev, yanıt üretimi, TX kuyruğu, fiziksel gönderim) büyüdüğünü ham veri ve grafikle göstermek.

Kapsam dışı: IWDG, çoklu buton, alternatif taşıyıcılar, PC–MCU saat senkronizasyonu (gerekmez; tüm süreler MCU'da ölçülür).

---

## 2. Mimari

```
 B1 (PI11) --EXTI11 her iki kenar--> [ISR] debounce, t0, event_id, ring slot
                                        | osMessageQueuePut (timeout 0)
                                        v
                                 ButtonQueue (8)
                                        |
                                        v
 [ButtonTask  Orta ] t1 -> yanit (BTN) uret -> t2 -> osMessageQueuePut
                                        |
 [TelemetryTask Yuksek] periyot + CPU yuku -> TEL ---+
                                        v            v
                                 TxQueue (16, FIFO, ortak)
                                        |
                                        v
 [UartTxTask  Dusuk] t3 -> HAL_UART_Transmit_DMA -> osSemaphoreAcquire(<=100 ms)
                     DMA2 Stream7 -> USART1 TX (PA9) --115200 8N1--> ST-LINK VCP --> PC
 [USART1 TC ISR] t4 -> osSemaphoreRelease

 PC --komut (CFG/START/STOP/DUMP)--> USART1 RX (PB7, IT) --> CmdQueue (4) --> UartTxTask (ayristirir)
 Deney sonu: UartTxTask RAM kayitlarini REC... STAT, DUMP_END olarak gonderir.
```

Görev listesi verilen tabloyla birebir aynıdır (3 görev). Komut kanalı 4. görev eklemez: RX ISR ham satırı `CmdQueue`'ya koyar, UART'ın sahibi olan `UartTxTask` ayrıştırır.

---

## 3. Donanım ve araç zinciri

| Bileşen | Değer | Kaynak / not |
|---|---|---|
| Kart | STM32F746G-DISCO, rev C01 (MB1191), MCU STM32F746NGH6, TFBGA216 | UM1907, şema paketi |
| Saat | HSE **25 MHz kristal** (bypass değil) → PLLM=25, PLLN=432, PLLP=2, PLLQ=9 → **SYSCLK 216 MHz**; AHB 216, APB1 54, APB2 108 MHz | Zephyr `stm32f746g_disco.dts` (`clk_hse` 25 MHz, `hse-bypass` yok; pll 25/432/2/9) |
| VCP UART | **USART1: TX=PA9, RX=PB7**, 115200 8N1 | Zephyr DTS (`usart1_tx_pa9`, `usart1_rx_pb7`), NuttX kart dokümanı |
| Buton | **B1 = PI11**, `GPIO_ACTIVE_HIGH` (basılınca YÜKSEK), dış pull devresi kartta | Zephyr DTS, NuttX kart dokümanı |
| USART1_TX DMA | DMA2 Stream7 Kanal 4, Normal, Mem→Periph, byte | F2/F4/F7 DMA istek eşleme tablosu (AN4031) |
| HAL zaman tabanı | **TIM6** (SysTick FreeRTOS'a ait) | FreeRTOS + HAL birlikte kullanımının gereği |
| Önbellek | I-Cache açık, **D-Cache kapalı** (DMA tutarlılığı; kod açık olursa da temizler, §12 #20) | Cortex-M7 |
| RTOS | FreeRTOS, CMSIS-RTOS v2, heap_4, 24 KB heap, 1 kHz tick, `configUSE_NEWLIB_REENTRANT=1` | `.ioc` içinde |
| Proje | **`firmware/Cubeide_Manuel/` — STM32CubeIDE projesi `UART_RTOS_ODEV1`** (kullanıcının oluşturduğu proje; `.ioc` deney için ayarlandı) | tek firmware projesi |
| IDE / CubeMX | **STM32CubeIDE 1.16.0** (içinde CubeMX 6.12.0) — bu makinede kurulu ve doğrulanan | En güncel: CubeIDE 2.2.0; 2.x'te CubeMX ayrı araçtır (6.17+) |
| MCU paketi | **STM32Cube FW_F7 V1.17.4** | STM32CubeF7 GitHub son etiketi (2025-08) |
| PC | Python ≥ 3.11 (doğrulama 3.12), PyQt6 ≥ 6.7 (doğrulama 6.11), pyserial ≥ 3.5, matplotlib ≥ 3.8, pandas ≥ 2.2 | `interface/requirements.txt` |

Aynı donanım değerleri `firmware/Cubeide_Manuel/UART_RTOS_ODEV1.ioc` ve `Core/Inc/app_config.h` içinde tanımlıdır; birini değiştiren diğerini de değiştirmelidir. Kart açılışta `BOOT` mesajıyla gerçek `SystemCoreClock` değerini ve DWT sayacının çalıştığını bildirir; arayüz 216 MHz / DWT OK değilse uyarır.

---

## 4. FreeRTOS görev mimarisi

| Görev | İstenen | CMSIS-RTOS v2 önceliği | Yığın | Görev |
|---|---|---|---|---|
| `UartTxTask` | Düşük · 1 | `osPriorityLow` (8) | 1536 B | TxQueue'yu tüketir, t₃/t₄, DMA gönderimi, komut işleme, dump |
| `ButtonTask` | Orta · 2 | `osPriorityNormal` (24) | 1024 B | Olayı alır, t₁, yanıt üretir, t₂, TxQueue'ya bırakır |
| `TelemetryTask` | Yüksek · 3 | `osPriorityAboveNormal` (32) | 1536 B | Periyodik TEL + sabit iterasyonlu CPU yükü |

Zamanlama: preemptive, 3 > 2 > 1. Periyotlar kernel tick cinsindendir (1 tick = 1 ms).

| Nesne | Derinlik × eleman | İçerik |
|---|---|---|
| `ButtonQueue` | 8 × 16 B | `ButtonEvent_t {t0, t0_abs_ms, event_id, ring_index, scenario_id}` |
| `TxQueue` (FIFO, ortak) | 16 × 70 B | `TxItem_t {type, event_id, ring_index, frame[64]}` |
| `CmdQueue` | 4 × 65 B | `CmdLine_t {len, text[64]}` (ham satır) |
| `TxDoneSem` | ikili | TC/DMA-hata ISR'ı → UartTxTask |
| `RunFlags` | `RUN_BIT` | START ile set, STOP ile temizlenir; TelemetryTask çalışmıyorken bloklu bekler |

Toplam RTOS belleği ≈ 8 KB (24 KB heap içinde). Oluşturma başarısızsa `Error_Handler()`.

**Kasıtlı davranış:** TelemetryTask en yüksek öncelikte olduğu için yükü ButtonTask'ı geciktirir (S1), TEL mesajları FIFO kuyrukta BTN'in önüne geçer (S3). Deneyin göstermek istediği budur.

---

## 5. Zaman damgalama

- **Kaynak:** Cortex-M7 `DWT->CYCCNT`. Açılış sırası: `DEMCR.TRCENA=1` → **`DWT->LAR = 0xC5ACCE55`** → `CYCCNT=0` → `CTRL.CYCCNTENA=1`. Cortex-M7'de LAR açılmazsa sayaç debugger bağlı değilken saymaz (bütün süreler 0 çıkar). `Timestamp_Init()` sayacın ilerlediğini kontrol eder, sonuç `BOOT` mesajında raporlanır.
- **Çözünürlük:** 216 MHz → 4.63 ns; µs'ye çevirme çalışma anındaki `SystemCoreClock` ile yapılır.
- **Taşma:** 32 bit, 216 MHz'de **~19.9 s**. İşaretsiz çıkarma tek taşmayı doğru verir (tek olayın t₀→t₄'ü en kötü ~1.6 s). Host testinde taşma üzerinden ölçüm doğrulandı.
- **Kronoloji:** `t0_abs_ms` ve TEL `t_abs_ms` = `HAL_GetTick()` − START anı (ikisi de aynı saat, ikisi de deney başlangıcına göre). Yalnızca sıralama/eksen içindir; **R hesabına girmez**. PC'nin alım zamanı hiçbir hesaba girmez.

---

## 6. t₀–t₄ ölçüm noktaları ve aşamalar

| Zaman | Kodda tam yeri | Tanım uyumu |
|---|---|---|
| t₀ | `Button_HandleEXTI()` ilk satırı (`app_button.c`) | ISR girişinde alınır; **yalnızca debounce filtresi kenarı kabul ederse** olayın t₀'ı olur. HAL EXTI dağıtıcısının önündeki birkaç yüz ns dahil. |
| t₁ | `ButtonTask`: `osMessageQueueGet` döndükten hemen sonra | ISR→görev aktarımı + CPU beklemesi dahil. |
| t₂ | `ButtonTask`: BTN yanıtı üretildikten sonra, `osMessageQueuePut`'tan hemen önce | Put başarılıysa zincir devam eder; başarısızsa `DROP`. Kayıt yazımı put'tan sonra (ölçümün dışında). |
| t₃ | `uart_tx_send()`: `HAL_UART_Transmit_DMA` çağrısından hemen önce | Kopyalama/önbellek temizliği öncesinde bitmiş olur; ilk fiziksel bit anı değildir. |
| t₄ | `HAL_UART_TxCpltCallback()` (USART1 TC ISR'ı) | Son bit sonrası TC işlenirken; en düşük öncelikli görevin uyanma gecikmesi **dahil değil**. |

| Aşama | Formül | Anlam |
|---|---|---|
| S1 | t₁ − t₀ | ISR→ButtonTask teslimi + CPU bekleme (TelemetryTask yükü burada görünür) |
| S2 | t₂ − t₁ | Yanıt üretimi (BTN çerçevesi: snprintf + CRC) |
| S3 | t₃ − t₂ | TX kuyruğunda bekleme (önündeki TEL/BTN çerçevelerinin gönderimi) |
| S4 | t₄ − t₃ | Fiziksel UART gönderimi (~5.56 ms sabit, §8.6) |
| **R** | t₄ − t₀ | Deadline: **R ≤ 20 000 µs** → `OK`, aşarsa `LATE` (µs cinsinden karşılaştırma) |

**Nedensellik:** t₂ yanıt üretildikten sonra, t₄ gönderim bittikten sonra bilinir; bu yüzden canlı BTN yanıtı yalnızca t₁−t₀'ı taşır. Tam kayıt RAM'de tutulur ve deney sonu gönderilir (§7).

---

## 7. RAM kayıt tamponu ve dump

- **Kayıt:** `EventRecord_t` = 32 B (t₀..t₄, t0_abs_ms, event_id, scenario_id, status, `ts_mask`, valid). `ts_mask` hangi zamanların gerçekten ölçüldüğünü tutar; ölçülmemişler dumpta `-` olur.
- **Kapasite:** 256 kayıt (≥ 64 gereksinimi), 8 KB.
- **Taşma:** En eskisinin üzerine yazılır, `ring_overflow_count++`. Olay kimlikleri her START'ta 1'den ardışık başladığı için PC, dumpta olmayan kimlikleri **kayıt kaybı** olarak sayar (bu sayı `ring_ovf` ile tutarlı olmalı).
- **Doğru olay kimliğiyle kapatma:** Her yazma `(ring_index, event_id)` ile yapılır; slot başka bir olaya geçmişse yazma reddedilir, `id_mismatch_count++` (normalde 0).
- **Eşzamanlılık:** Her güncelleme kısa kritik bölgede (`taskENTER_CRITICAL` / `_FROM_ISR`); mutex yok.
- **Dump akışı:** PC `STOP` → (400 ms) → `DUMP`. Kart DUMP'ı yalnızca RUNNING=0 **ve** TxQueue boşken kabul eder; aksi halde yok sayar. PC 3 s içinde REC/STAT gelmezse DUMP'ı en fazla 3 kez yeniden ister (her denemede alınanları siler; kart hepsini baştan gönderir). Dump sırası: `REC` × N (eskiden yeniye) → `STAT` → `DUMP_END`.
- **Neden deney sonu:** Sonuçları deney sırasında göndermek, ölçülen şeyin kendisini (TX kuyruğu, UART bant genişliği) yükler — "ölçüme karışmama" ilkesiyle çelişir.

---

## 8. UART protokolü

### 8.1 Fiziksel katman
115200 baud, 8N1.

### 8.2 Çerçeve (tüm MCU→PC mesajları) — 64 bayt

| Bayt | İçerik |
|---|---|
| 0..61 | ASCII alanlar (virgülle ayrılmış), sonu boşlukla doldurulur |
| 62 | CRC-8 (ham bayt), bayt 0..61 üzerinden |
| 63 | `\n` |

- İlk isteğe göre "63 bayt ASCII + LF" idi; 1 baytlık CRC kararıyla ASCII alan 62 bayta indi. CRC baytı ham ikili olduğu için çerçevenin tamamı yazdırılabilir değildir.
- CRC baytı 0x0A olabilir. Hizalı okumada karar yalnızca bayt 63'e göre verildiği için sorun değildir; hizalama kaymasında yeniden senkron bir adım uzayabilir, kendiliğinden toparlanır.
- **Uzun mesaj sessizce kesilmez:** içerik 62 bayta sığmazsa çerçeve `ERR,ENCODE_OVERFLOW,<tip>,<uzunluk>` olur ve kodlayıcı `false` döner; PC bunu "Kart mesajları" listesinde gösterir.
- **CRC-8:** poly 0x07, init 0x00, yansıtma yok, xorout yok. Kontrol değeri: `CRC("123456789") = 0xF4` (C ve Python tarafında test edildi).

### 8.3 MCU → PC mesajları (örnekler firmware kodlayıcısının gerçek çıktısıdır)

| Tip | Alanlar | Örnek |
|---|---|---|
| `BOOT` | sysclk_hz, dwt_ok, fw | `BOOT,216000000,1,0.3` |
| `TEL` | seq, scenario, t_abs_ms, period_ms, load_iter, **load_us** (yük döngüsünün DWT ile ölçülen süresi), drop (buton+TX kuyruğu), tx_fail (BTN TX_ERR+TIMEOUT), ring_ovf | `TEL,42,3,125340,10,5000,3120,3,7,5` |
| `BTN` | event_id, scenario, t0_abs_ms, d1_us (=t₁−t₀), `PRESSED` | `BTN,123,3,125341,82,PRESSED` |
| `REC` | event_id, scenario, t0_abs_ms, d1, d2, d3, d4 (µs, **t₀'a göre**; ölçülmediyse `-`), status | `REC,123,3,125341,82,97,4210,9770,OK` · `REC,125,3,125341,82,97,-,-,DROP` |
| `STAT` | scenario, success, drop, tx_err, timeout, ring_ovf, cmd_crc_err, tel_drop, tel_tx_fail, id_mismatch | `STAT,3,58,3,3,4,5,6,7,8,0` |
| `DUMP_END` | — | `DUMP_END` |
| `ERR` | neden, … | `ERR,ENCODE_OVERFLOW,TEL,106` |

`status`: `OK`, `LATE`, `DROP` (ButtonQueue veya TxQueue dolu), `TX_ERR` (DMA başlatılamadı / DMA hatası), `TIMEOUT` (TC 100 ms'de gelmedi), `PENDING` (kapanmamış — olmamalı).
Alan bütçesi: en uzun gerçekçi REC ≈ 59 bayt (7 haneli süreler dahil).

### 8.4 PC → MCU komutları

Satır biçimi `<içerik>,<CRC-8 iki haneli büyük harf hex>\n`; CRC içerik üzerinden (son virgül hariç). Değişken uzunluklu satırda LF ile karışmasın diye CRC hex yazılır. Aşağıdaki CRC'ler gerçek değerlerdir:

| Komut | Satır | Ne zaman kabul |
|---|---|---|
| CFG | `CFG,10,5000,3,82` | RUNNING=0. Periyot < 10 ms ise 10'a çekilir. |
| START | `START,98` | RUNNING=0 → sayaçlar, kayıtlar, olay kimliği sıfırlanır |
| STOP | `STOP,66` | RUNNING=1 |
| DUMP | `DUMP,22` | RUNNING=0 ve TxQueue boş |
| RESET_STATS | `RESET_STATS,6A` | RUNNING=0 |

Sayısal alanlar yalnızca ondalık rakam kabul eder; eksik/fazla alan, küçük harf hex, bilinmeyen komut reddedilir. CRC uyuşmazlığı `cmd_crc_err` sayacını artırır. Arayüz "Kaydı Başlat"ta **CFG ve START'ı birlikte** gönderir ve ilk TEL ile kartın gerçekten bu ayarlarla koştuğunu doğrular (`config_verified`).

### 8.5 CRC hatası davranışı
MCU→PC: PC çerçeveyi atar, `crc_error_count++`. PC→MCU: MCU komutu yok sayar, `cmd_crc_err++`. Her ikisi de `record_loss`/`drop`'tan ayrı sayılır.

### 8.6 Bant genişliği (kritik kısıt)
8N1'de bayt 10 bit → 86.8 µs; **64 baytlık çerçeve ≈ 5.56 ms** (S4'ün tabanı). Dolu TxQueue ≈ 16 × 5.56 ≈ 89 ms. Önünde 3 TEL olan bir BTN yalnızca S3'te ~16.7 ms bekler. Periyot 5.56 ms'nin altına inerse yalnızca telemetri hattı doldurur; bu yüzden **minimum periyot 10 ms** (kartta ve arayüzde zorunlu).

---

## 9. Senaryo matrisi (başlangıç önerisi, kalibrasyonla kesinleşecek)

| Senaryo | Periyot | CPU yükü | Amaç |
|---|---|---|---|
| S0 | 200 ms | 0 | Taban (S4 ≈ 5.56 ms, S3 ≈ 0) |
| S1 | 50 ms | 0 | Normal telemetri |
| S2 | 15 ms | 0 | Yüksek hız → S3 (kuyruk) etkisi yalnız |
| S3 | 50 ms | 15 ms (periyodun %30'u) | CPU yükü → S1 etkisi |
| S4 | 15 ms | 4.5 ms (%30) | Birleşik stres |
| S5 | 15 ms | 12 ms (%80) | Aşırı yük: deadline kaçırma, düşme |

`load_iter` → süre dönüşümü kartta ölçülür: her TEL, o periyottaki yük döngüsünün gerçek süresini (`load_us`) taşır ve arayüz bunu periyoda oranıyla gösterir (kalibrasyon adımları `docs/code-notes.md`). Yük ≥ periyot olduğunda TelemetryTask kaçırılan periyotları telafi etmeye çalışmaz, yeniden hizalanıp yine bir periyot uyur (§12 #13).

---

## 10. PC arayüzü

- **İş parçacığı:** Seri port yalnızca `SerialWorker` (QThread) içinde; GUI'ye ham 64 B çerçeveler Qt sinyaliyle iletilir, çözümleme oturum içinde yapılır.
- **Düzen (kullanıcının referans ekranına göre):** bağlantı satırı; deney satırı (varyant A/B/C, senaryo, periyot, yük ms, *Deneyi başlat*, *Durdur ve kayıtları al*, *Sentetik demo*); bilgi bandı; canlı durum (`RUNNING / TEL / BTN`, *Butona basıldı / olay N*); oturum satırı (*Oturum dosyası aç*, *Ham oturumu kaydet*, *CSV kaydet*); KPI'lar (başarılı, R min/ort/maks, 20 ms aşımı, başarısız/kayıt kaybı); sekmeler: Grafikler, A/B/C karşılaştırma, Olay kayıtları, Sayaçlar ve rehber.
- **Canlılık:** durum sayaçları, basış göstergesi, olay tablosu (t₁−t₀) ve grafikteki "bekleniyor" işaretleri deney sırasında anlık güncellenir; R değerleri t₄ bilindikten sonra, DUMP ile gelir (§7).
- **Oturum:** her deney ham çerçeveleriyle saklanır (`measurements/sessions/*.json`); açılan dosya yeniden çözülerek aynı sonucu verir. `S<n>.csv` de açılabilir.
- **Yük:** ms olarak girilir; iterasyona dönüşüm karttan gelen `load_us` ile kalibre edilir (`interface/calibration.json`).
- **Sentetik demo:** firmware zamanlama mekanizmasının simülasyonu (kartta ölçülen temel sürelerle); gerçek kartla aynı çerçeveleri üretir; `measurements/` altına yazmaz.
- **Deney sırasında** PC karta yalnızca STOP gönderir; ayarlar kayıt sürerken kilitlenir.
- **Çerçeve ayrıştırma:** 64 bayt + LF + CRC doğrulaması; uymayan atılır ve sayılır; kaymada bir sonraki LF'ye kadar atlanır.

---

## 11. Veri şemaları

**`measurements/S<n>.csv`** — her satır bir buton olayı; ölçülmemiş alanlar **boş**:
```
event_id, scenario_id, t0_abs_ms, t1_us, t2_us, t3_us, t4_us, R_us,
stage1_us, stage2_us, stage3_us, stage4_us, status
```
`t<i>_us` = tᵢ − t₀. `R_us` ve `stage4_us` yalnızca `OK`/`LATE` için doludur.

**`measurements/summary.csv`** — her satır bir senaryo; aynı senaryo tekrar koşulursa satırı **değiştirilir** (S<n>.csv de üzerine yazıldığı için):
```
scenario_id, period_ms, load_iter, config_verified,
events_total, success_count, R_min_us, R_mean_us, R_max_us, late_count_over_20ms,
drop_count, tx_err_count, timeout_count, pending_count,
record_loss_count, ring_overflow_count,
crc_error_count, parse_error_count, cmd_crc_err_count,
tel_drop_count, tel_tx_fail_count, id_mismatch_count,
stage1_mean_us, stage2_mean_us, stage3_mean_us, stage4_mean_us
```

**Grafikler** diskteki ham CSV'lerden okunarak çizilir ve `analysis/plots/` altına kaydedilir (`S<n>_R.png`, `stages_by_scenario.png`). `python interface/plotting.py` hepsini yeniden üretir.

---

## 12. Hata senaryoları ve çözümler

| # | Senaryo | Neden | Çözüm (uygulandığı yer) |
|---|---|---|---|
| 1 | Basmada kontak sekmesi | Mekanik anahtar | Kenar, önceki **herhangi bir** kenardan ≥ 30 ms sessizlik sonra gelmeli ve pin basılı seviyede olmalı (`app_button.c`) |
| 2 | Bırakmada sekme sahte basma üretir | Yalnız yükselen kenar dinlenirse 30 ms'den uzun basışın bırakma sekmesi yeni basma sayılır | EXTI **her iki kenar**; bırakma kenarları sessizlik süresini yeniler (`.ioc`, `app_button.c`). Kalan risk: ilk basma kenarında pin < 1 µs içinde tekrar düşerse o basma kaçar (belgelendi) |
| 3 | ButtonQueue dolu | ButtonTask çok gecikmiş | ISR'da timeout 0; `DROP`, `btn_queue_drop++` |
| 4 | TxQueue dolu (BTN) | Hız + yük | Put ≤ 200 ms; başarısızsa `DROP`, `tx_queue_drop++` |
| 5 | TxQueue dolu (TEL) | Aynı | `tel_drop++`, STAT'ta raporlanır |
| 6 | UART/DMA başlatılamadı | Periferik meşgul | Anında `TX_ERR`, beklemeye girilmez |
| 7 | TC hiç gelmiyor | Donanım/DMA takılması | Semaphore ≤ 100 ms; `HAL_UART_AbortTransmit`; `TIMEOUT` |
| 8 | Geç gelen TC sonraki gönderimi erken bitirir | Timeout ile TC yarışı | Her gönderim öncesi semaphore'daki eski jeton boşaltılır |
| 9 | Aktarım bitmeden tampon üzerine yazılır | Tampon erken yeniden kullanımı | Tek sahipli tampon; TC/timeout+abort olmadan sonraki mesaja geçilmez |
| 10 | t₄ yanlış yerde ölçülür | En düşük öncelikli görev uyanınca ölçmek araya giren görevleri S4'e ekler | t₄ TC ISR'ında (`app_hal_callbacks.c`) |
| 11 | Kayıt yanlış olaya yazılır | Tampon dönüşü sırasında eski indeks | `(ring_index, event_id)` doğrulaması, `id_mismatch` |
| 12 | Kayıt tamponu taşması | >256 olay | Üzerine yazma + `ring_ovf`; PC eksik kimlikleri "kayıt kaybı" sayar ve grafikte gösterir |
| 13 | TelemetryTask ani TEL patlaması | Eski `wake` değeri (START öncesi bekleme) | `wake` her START'ta tazelenir |
| 14 | Aşırı yükte tüm alt görevler aç kalır (STOP bile işlenemez) | Yük ≥ periyot → en yüksek görev hiç bloklanmaz | Gecikmişse yeniden hizala ve yine bir periyot uyu |
| 15 | STOP yarışında kilitlenme | Bayrak/değişken sırası | STOP'ta önce event flag temizlenir, sonra `g_running=false` |
| 16 | UART RX hatası komut kanalını öldürür | HAL overrun'da RX IT zincirini durdurur; eski kod ayrıca TX hatası sanıyordu | Hata kodu ayrıştırılır: yalnızca `HAL_UART_ERROR_DMA` TX hatasıdır; ORE/FE/NE/PE'de RX yeniden kollanır |
| 17 | Bozuk/uzun komut | Gürültü | CRC-8, katı sayı ayrıştırma, 64 bayt sınırı, LF'ye kadar atma |
| 18 | Uzun mesaj sessizce kesilir | `snprintf` sessizce keser | Dönüş değeri kontrolü → `ERR,ENCODE_OVERFLOW` |
| 19 | DWT saymaz, tüm süreler 0 | Cortex-M7 LAR kilidi | `DWT->LAR = 0xC5ACCE55`; BOOT'ta `dwt_ok` |
| 20 | DMA eski veriyi gönderir | D-Cache açıkken CPU yazısı RAM'e inmemiş | D-Cache kapalı; açılırsa 32 B hizalı tamponda `SCB_CleanDCache_by_Addr` |
| 21 | Deadline yanlış sınıflanır | ms'ye kesme | µs ile karşılaştırma |
| 22 | DROP olayı R=0 görünür | Ölçülmemiş = 0 | `ts_mask` → `-` → CSV'de boş |
| 23 | CSV ayarları kartın ayarlarıyla uyuşmaz | CFG gönderilmeden START | Başlat = CFG + START; ilk TEL ile doğrulama |
| 24 | DUMP yok sayılır, PC sonsuza kadar bekler | TxQueue boş değil | 3 s zaman aşımı, 3 deneme, kullanıcıya bildirim |
| 25 | summary.csv'de eski/tekrarlı satır | Ekleme (append) | Senaryo kimliğine göre değiştirme (upsert) |
| 26 | Bağlantı kopar | USB çekildi | Kısmi veri kaydedilir, arayüz çökmez |
| 27 | Çerçeve kayması / bozuk bayt | Gürültü | LF + CRC doğrulaması, yeniden senkron |
| 28 | newlib yeniden girişsizliği | 3 görev snprintf kullanıyor | `configUSE_NEWLIB_REENTRANT=1`; ayrıştırma ISR'dan göreve taşındı |
| 29 | Yığın taşması | snprintf yığını | ARM derleyicisinin `-fstack-usage` çıktısıyla ölçüldü: en derin zincir ≈ 1.1 KB (UartTxTask dump), ≈ 0.8 KB (TelemetryTask), ≈ 0.7 KB (ButtonTask); yığınlar 1536 / 1024 / 1536 B; `configCHECK_FOR_STACK_OVERFLOW=2` + CubeMX'in `freertos.c`'deki hook'u (bkz. #35) |
| 30 | Kod üretimi derlemeyi bozar | "Peripheral başına .c/.h" kapalıyken (`CoupleFile=false`, bu projede) `usart.h` üretilmez, `huart1` yalnızca `main.c`'de tanımlıdır | Uygulama `usart.h`'a bağlı değil; `app_hw.h` `huart1`'i `extern` bildirir — iki düzende de derlenir |
| 31 | CubeMX dosya çakışması | Bazı ailelerde CubeMX `app_freertos.c` üretir | Uygulama dosyası `app_rtos.c` olarak adlandırıldı |
| 32 | Windows'ta `S*.csv` deseni `summary.csv`'yi yakalar | Büyük/küçük harf duyarsız dosya sistemi | Tam ad eşleştirmesi (`plotting.py`) — test sırasında bulundu |
| 33 | Yanlış USART parametre değeri | F7 USART IP'sinde aşırı örnekleme değeri `UART_OVERSAMPLING_16` olmalı | CubeMX IP veritabanından doğrulanıp `.ioc`'ta düzeltildi |
| 34 | FreeRTOS tick hiç çalışmaz | HAL zaman tabanı TIM iken `SysTick_Handler` FreeRTOS portuna eşlenmezse başlangıç dosyasının boş döngüsü kalır | CubeMX (FW_F7 1.17.x, FreeRTOS 10.2) `FreeRTOSConfig.h`'a `#define xPortSysTickHandler SysTick_Handler` yazar; kontrol listesinde |
| 35 | Hook çift tanımı | CMSIS_V2'de CubeMX stack overflow / malloc-failed hook'larını `freertos.c`'de güçlü tanım olarak üretir; uygulamada ikinci tanım link hatası verir | Uygulama hook tanımlamaz; CubeMX'in (boş) hook'ları olduğu gibi kullanılır. Sonuç: taşma tespit edilir ama hook boş olduğu için sistem durmaz — gerekirse hook gövdesini kullanıcı `freertos.c`'de doldurur |
| 36 | `defaultTask` ölçümü etkileyebilir | CubeMX (CMSIS_V2) en az bir görev tutar; `defaultTask` ButtonTask ile aynı öncelikte (`osPriorityNormal`) her 1 ms uyanır | CubeMX kodu korunur. Etkisi µs mertebesinde; istenirse CubeMX'te *FREERTOS → Tasks and Queues → defaultTask → Priority* `osPriorityIdle`/`osPriorityLow` yapılır |

---

## 13. Kabul kriterleri

- Her senaryo için Deneyi başlat → buton basışları → Durdur ve kayıtları al → dump tamamlanmış; `S<n>.csv` ve `summary.csv` satırı var, `config_verified=1`, `id_mismatch_count=0`, `pending_count=0`.
- `summary.csv`: başarılı ölçüm, R min/ort/maks, >20 ms sayısı, drop/tx_err/timeout/kayıt kaybı, aşama ortalamaları dolu.
- İki grafik `analysis/plots/` altında ve ham CSV'den üretilmiş.
- `BOOT` mesajı 216 MHz ve DWT OK gösteriyor.

---

## 14. Klasör yapısı

```
freertos-bootcamp/hafta-01/
├── README.md
├── SPEC.md
├── firmware/
│   ├── README.md
│   └── Cubeide_Manuel/          STM32CubeIDE projesi UART_RTOS_ODEV1
│       ├── UART_RTOS_ODEV1.ioc  pinler, saat, DMA, NVIC, FreeRTOS
│       └── Core/{Inc,Src}/      app_*.c/h (uygulama) + CubeMX dosyaları
├── interface/                   PyQt6 arayüzü
├── measurements/                S0..S5.csv, summary.csv (gerçek koşumlarla oluşur)
├── analysis/{report.md, plots/}
└── docs/{setup.md, code-notes.md, ai-usage.md}
```

---

## 15. Doğrulama durumu ve açık konular

**Yapılan doğrulamalar:**
- Tüm firmware kaynakları, CMSIS-RTOS2 ve HAL'ın gerçek imzalarını taklit eden başlıklarla `gcc -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes` altında hatasız derlendi.
- Gerçek `app_protocol.c` / `app_ring_buffer.c` / `app_timestamp.c` host üzerinde çalıştırıldı: CRC kontrol değeri, çerçeve biçimi, DWT taşması, µs deadline sınırı, tampon taşması/sırası, olay kimliği doğrulaması, kritik bölge dengesi.
- C↔Python çapraz test: Python'un ürettiği komutları C, C'nin ürettiği çerçeveleri Python doğru çözdü; bozuk CRC ve 8 hatalı komut reddedildi.
- Arayüz, başsız (offscreen) uçtan uca test edildi (33 kontrol: kart yolu, DUMP yeniden deneme, oturum kaydet/aç, kalibrasyon, sentetik demo, kullanıcının gerçek S0/S1 ölçümlerini açma). Python çerçeve kodlayıcısı C kodlayıcısıyla bayt bayt aynı.
- **Gerçek ARM derlemesi:** uygulama, CubeIDE 1.16.0'ın GCC 12.3'ü ve FW_F7 1.17.4'ün gerçek HAL/CMSIS/FreeRTOS/CMSIS-RTOS2 kaynaklarıyla, CubeMX'in üreteceği başlatma kodunun eşdeğeriyle uyarısız derlenip link edildi (flash 45 KB, RAM 39 KB); bağlanan sembol sahipleri ve yığın kullanımı ELF/map üzerinden kontrol edildi.

**Hâlâ doğrulanmamış (bu ortamda mümkün değil):**
1. CubeMX kod üretimi (`.ioc` değişiklikleri CubeMX 6.12'de yüklenip saat ağacı doğrulandı; kod üretimini kullanıcı yapacak).
2. S2–S5 ölçümleri ve yük kalibrasyonu. (S0 ve S1 kartta ölçüldü, 2026-09-22: tüm olaylar OK;
   t₁−t₀ ≈ 16 µs, t₂−t₁ ≈ 82 µs, t₄−t₃ ≈ 5563 µs — tasarımla tutarlı.)
