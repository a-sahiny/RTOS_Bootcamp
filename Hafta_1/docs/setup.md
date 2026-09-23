# Kurulum

## 1. Donanım

- STM32F746G-DISCO rev C01.
- Tek USB kablo: kartın **ST-LINK USB** konnektörü → PC. Besleme, yükleme/debug ve
  sanal COM portu (VCP) aynı kablodan gelir. Ek kablo veya USB-UART dönüştürücü gerekmez.
- Kullanılan kart kaynakları:

| İşlev | Pin / periferik | Kaynak |
|---|---|---|
| VCP TX | PA9 — USART1_TX (AF7) | Zephyr `stm32f746g_disco.dts`: `usart1_tx_pa9` |
| VCP RX | PB7 — USART1_RX (AF7) | Zephyr DTS: `usart1_rx_pb7` |
| Kullanıcı butonu | PI11 — `GPIO_ACTIVE_HIGH` | Zephyr DTS: `gpios = <&gpioi 11 GPIO_ACTIVE_HIGH>`; NuttX kart dokümanı |
| HSE | 25 MHz kristal, bypass yok | Zephyr DTS: `clk_hse` 25 MHz, `hse-bypass` tanımlı değil |
| SWD | PA13 / PA14 | ST-LINK |

## 2. Yazılım

1. **STM32CubeIDE 1.16.0** (bu makinede kurulu; içinde CubeMX 6.12.0). *Help → Manage
   embedded software packages → STM32F7 → 1.17.4* kurulu olmalı (kurulu). En güncel sürüm
   2.2.0'dır; 2.x'e geçilirse `.ioc` ayrı STM32CubeMX (6.17+) ile açılır.
2. Sürücüler: ST-LINK ve VCP sürücüleri kurulumla gelir; Windows
   Aygıt Yöneticisi'nde kart takılıyken *Bağlantı noktaları → STMicroelectronics
   STLink Virtual COM Port (COMx)* görünmelidir. Görünmüyorsa ST-LINK sürücüsünü
   (STSW-LINK009) ayrıca kurun.
3. **Python ≥ 3.11** (bu makinede 3.12, `py -3` ile). En kolayı `interface\run_gui.bat`'ı çalıştırmak;
   ilk seferde sanal ortamı kendisi kurar. Elle:

```bash
cd interface
py -3 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

Bu projede doğrulanan sürümler: Python 3.12, PyQt6 6.11, pyserial 3.5, matplotlib 3.9, pandas 2.2.

## 3. İlk çalıştırma kontrolü

1. `firmware/README.md` adımlarıyla üretin, derleyin, yükleyin.
2. `interface\run_gui.bat` → COM portu seçin → Bağlan → karttaki siyah RESET'e basın.
3. Beklenen: `BOOT: 216 MHz, DWT OK, fw 0.3`.

| Belirti | Olası neden |
|---|---|
| Hiç mesaj yok | Yanlış COM portu; başka bir program (CubeIDE terminali vb.) portu tutuyor; USART1 pinleri PA9/PB7 değil |
| `DWT CALISMIYOR` | `Timestamp_Init()` çağrılmadı veya LAR kilidi açılmadı (SPEC §12 #19) |
| 216 dışında MHz | `.ioc` saat ayarı değişmiş / HSE başlatılamadı |
| Sürekli CRC hatası | Baud hızı uyuşmuyor; D-Cache açık ve temizlenmiyor (SPEC §12 #20) |
| Buton hiç olay üretmiyor | Kayıt başlatılmadı (RUNNING=0 iken basmalar yok sayılır); PI11 EXTI ayarı |
