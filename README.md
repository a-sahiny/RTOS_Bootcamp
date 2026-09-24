# Hafta 01 — Yük Altında Buton Yanıt Süresi

Butona basıldığında orta öncelikli bir FreeRTOS görevi "butona basıldı" yanıtını
üretir; yanıt ortak TX kuyruğundan DMA ile UART'a çıkar. Yanıt süresi
**R = t₄ − t₀** kart üzerinde DWT cycle sayacıyla ölçülür, telemetri hızı ve
yapay CPU yükü değiştirilerek gecikmenin hangi aşamada büyüdüğü PC arayüzünde
ham veri ve grafikle gösterilir. Tasarımın tamamı: [SPEC.md](SPEC.md).

## 1. Kart, bağlantılar ve araç sürümleri

| | |
|---|---|
| Kart | STM32F746G-DISCO rev C01 (STM32F746NGH6, Cortex-M7, 216 MHz) |
| Bağlantı | Tek USB kablo: kartın **ST-LINK USB** konnektörü → PC. Aynı kablo besleme, yükleme/debug ve sanal COM portu (VCP) sağlar. |
| UART | USART1 — TX **PA9**, RX **PB7** → ST-LINK VCP, 115200 8N1 |
| Buton | Mavi kullanıcı butonu B1 — **PI11**, basılınca YÜKSEK |
| Ek donanım | Yok |

| Araç | Sürüm |
|---|---|
| STM32CubeIDE (içinde CubeMX 6.12.0) | 1.16.0 — bu makinede kurulu ve doğrulanan (en güncel: 2.2.0) |
| STM32Cube FW_F7 paketi | V1.17.4 |
| FreeRTOS arayüzü | CMSIS-RTOS v2 (CubeMX paketiyle gelen çekirdek) |
| Python | ≥ 3.11 (3.12 ile doğrulandı) |
| PyQt6 / pyserial / matplotlib / pandas | ≥ 6.7 / 3.5 / 3.8 / 2.2 (bkz. `interface/requirements.txt`); `run_gui.bat`'ın kurduğu 6.11 / 3.5 / 3.11 / 3.0 ile doğrulandı |

Kurulum ayrıntıları ve sürücüler: [docs/setup.md](docs/setup.md).

## 2. Derleme, yükleme ve arayüzü başlatma

**Firmware** (ayrıntı: [firmware/README.md](firmware/README.md)):

1. STM32CubeIDE'de `firmware/Cubeide_Manuel` projesini (UART_RTOS_ODEV1) açın.
2. `UART_RTOS_ODEV1.ioc` → **Project → Generate Code**.
3. *Build* → *Debug As → STM32 C/C++ Application* (ST-LINK).

Kod üretiminden sonra elle düzenleme gerekmez (`main.c`'deki başlatma çağrısı
korunan USER CODE bloğunda).

**Arayüz:**

`interface\run_gui.bat` dosyasına çift tıklayın. İlk çalıştırmada `interface\.venv` sanal
ortamını kurup `requirements.txt`'yi yükler (internet gerekir, birkaç dakika sürer); sonraki
çalıştırmalarda doğrudan açılır. `run_gui.bat check` pencere açmadan kurulumu doğrular.

Bat dosyası Python'u `py -3` ile seçer: bu makinede `cmd` içinde düz `python` önce MSYS2'nin
Python'unu buluyor ve PyQt6 onda çalışmıyor.

Elle kurmak isterseniz:

```bash
cd interface
py -3 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
python app.py
```

Portu seçin → **Bağlan** → kartı resetleyin: "Kart mesajları" listesinde
`BOOT: 216 MHz, DWT OK` görünmelidir.

## 3. Senaryo seçimi ve ölçüm adımları

1. **Varyant** (A/B/C etiketi), **senaryo** (S0–S5), gerekirse **periyot** (≥ 10 ms) ve **yük** (ms) seçin.
2. **Deneyi başlat** — arayüz CFG ve START'ı birlikte gönderir; ilk TEL ile kartın bu
   ayarlarla çalıştığını doğrular. Deney sürerken ayarlar kilitlenir.
3. Butona istediğiniz sayıda basın (debounce 30 ms). Her basış anında "Butona basıldı / olay N",
   olay tablosu ve grafikte "bekleniyor" işareti olarak görünür.
4. **Durdur ve kayıtları al** — STOP gönderilir, TX kuyruğu boşalınca kart RAM'deki tüm
   kayıtları gönderir. R değerleri ve KPI'lar dolar; `measurements/S<n>.csv`, `summary.csv`,
   `analysis/plots/` ve ham oturum (`measurements/sessions/`) otomatik kaydedilir.

Kart olmadan denemek için **Sentetik demo** (ölçüm değildir, `measurements/`'a yazmaz).
Önceki ölçümler **Oturum dosyası aç** ile (`.json` veya `S<n>.csv`) panele yüklenebilir.
5. Diğer senaryolar için tekrarlayın. Aynı senaryo tekrar koşulursa önceki sonucun
   yerini alır.

Önerilen başlangıç matrisi ([SPEC.md §9](SPEC.md)):

| | Periyot | Yük | Amaç |
|---|---|---|---|
| S0 | 200 ms | 0 | taban |
| S1 | 50 ms | 0 | normal |
| S2 | 15 ms | 0 | kuyruk etkisi (S3) |
| S3 | 50 ms | 15 ms | CPU etkisi (S1) |
| S4 | 15 ms | 4.5 ms | birleşik |
| S5 | 15 ms | 12 ms | aşırı yük |

Yük ms olarak girilir; iterasyona çevrim ilk yüklü deneyde karttan ölçülerek kalibre edilir
([interface/README.md](interface/README.md)).

## 4. Timer ve FreeRTOS ayarları

| Ayar | Değer |
|---|---|
| Sistem saati | HSE 25 MHz → PLL → 216 MHz (APB1 54, APB2 108 MHz) |
| Zaman damgası | Cortex-M7 **DWT CYCCNT** (4.63 ns çözünürlük, ~19.9 s'de taşar; LAR kilidi açılır) |
| FreeRTOS tick | SysTick, 1 kHz (periyotlar ve zaman aşımları) |
| HAL timebase | **TIM6**, 1 kHz (debounce ve deney-içi ms zamanı) |
| Görevler | TelemetryTask `osPriorityAboveNormal` > ButtonTask `osPriorityNormal` > UartTxTask `osPriorityLow`, preemptive |
| Kuyruklar | ButtonQueue 8, TxQueue 16 (FIFO), CmdQueue 4 |
| TX | DMA2 Stream7, görev TC gelene kadar bloklanır (≤ 100 ms, sonra abort) |
| Kesme öncelikleri | USART1 / DMA2_Stream7 / EXTI15_10 = 5 (FreeRTOS API çağrılabilir), TIM6 / SysTick / PendSV = 15 |
| Heap | heap_4, 24 KB (≈ 8 KB kullanılır) |
| Kayıt tamponu | 256 olay (8 KB) + taşma sayacı |

## 5. Ham veri, grafikler ve rapor

| | |
|---|---|
| Ham veri (her satır bir buton olayı) | `measurements/S0.csv` … `measurements/S5.csv` |
| Senaryo özeti | `measurements/summary.csv` |
| Olay # → R (20 ms çizgisi, kayıplar işaretli) | `analysis/plots/S<n>_R.png` |
| Senaryo → aşama süreleri (yığılmış sütun) | `analysis/plots/stages_by_scenario.png` |
| Rapor | [analysis/report.md](analysis/report.md) |
| Kod notları | [docs/code-notes.md](docs/code-notes.md) |
| Yapay zekâ kullanımı | [docs/ai-usage.md](docs/ai-usage.md) |

Grafikler her zaman diskteki ham CSV'den üretilir; yeniden üretmek için
`python interface/plotting.py`.

> **Durum:** Kart üzerinde S0 ve S1 ölçüldü (`measurements/S0.csv`, `S1.csv`); S2–S5 ve
> yük kalibrasyonu sıradaki adımdır (bkz. [SPEC.md §15](SPEC.md)).
