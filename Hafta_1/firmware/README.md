# Firmware — STM32F746G-DISCO (C01)

Tek firmware projesi: **`firmware/Cubeide_Manuel/`** (STM32CubeIDE projesi
`UART_RTOS_ODEV1`, CubeMX yapılandırması `UART_RTOS_ODEV1.ioc`).

| Konum | İçerik | Kim yazar |
|---|---|---|
| `UART_RTOS_ODEV1.ioc` | pinler, saat, DMA, NVIC, FreeRTOS ayarları | CubeMX (deney için ayarlandı, bkz. §2) |
| `Core/Inc/app_*.h`, `Core/Src/app_*.c` | uygulama: görevler, protokol, kayıt tamponu, zaman damgası | elle — CubeMX dokunmaz |
| `Core/Src/main.c` → `USER CODE BEGIN Includes` ve `RTOS_THREADS` | `#include "app_rtos.h"` ve `App_RTOS_Init();` | elle — CubeMX kod üretiminde korur |
| `Drivers/`, `Middlewares/`, geri kalan `Core/` dosyaları, `.project`, `.cproject`, linker script | HAL, CMSIS, FreeRTOS, başlangıç kodu | CubeMX |

## 1. Araçlar

Bu makinede doğrulanan: **STM32CubeIDE 1.16.0** (içinde CubeMX 6.12.0) ve
**STM32Cube FW_F7 V1.17.4** (en güncel F7 paketi). STM32CubeIDE 2.x'e geçilirse CubeMX
IDE'nin içinde olmaz; `.ioc` ayrı STM32CubeMX (6.17+) ile açılır, proje CubeIDE'de
*File → Import → Existing Projects into Workspace* ile açılır.

## 2. Kod üretme ve derleme

1. CubeIDE'de `UART_RTOS_ODEV1.ioc`'u açın. (Değişiklikler dosyaya yazıldı; açık bir
   sekme varsa kaydetmeden kapatıp yeniden açın.)
2. §3'teki listeyi gözle kontrol edin.
3. **Project → Generate Code** (veya Ctrl+S → "Generate code?" → Yes).
   - "USE_NEWLIB_REENTRANT must be Disabled with EWARM OR MDK-ARM" uyarısı yalnızca
     IAR/Keil içindir; STM32CubeIDE (GCC) için **açık kalmalıdır** — üç görev `snprintf`
     kullanıyor. Uyarıyı geçin.
   - "USART1 not configured / wrong parameter" uyarısı artık çıkmamalı (hatalı
     `OverSampling` değeri düzeltildi). Çıkarsa üretmeyin ve USART1 sekmesine bakın.
4. *Project → Build* (Ctrl+B), ardından *Run → Debug As → STM32 C/C++ Application*.

Kod üretimi sonrası `main.c`'de USART6 kodu kalkar; yerine USART1, DMA2 Stream7 ve PI11
EXTI başlatması gelir. `USER CODE` blokları korunur.

Elle doldurulan USER CODE blokları (yalnızca CubeMX'in boş bıraktığı bloklar; CubeMX'in ürettiği
kod hiçbir yerde değiştirilmedi):

| Dosya / blok | İçerik | Neden |
|---|---|---|
| `main.c` Includes | `#include "app_rtos.h"` | |
| `main.c` RTOS_THREADS | `App_RTOS_Init();` | 3 görev + kuyruklar |


## 3. `.ioc`'ta yapılan değişiklikler

Dosyanın önceki hâli oturum yedeğinde saklandı. Değiştirilenler (kararlar sizin):

| Ayar | Önce | Sonra | Neden |
|---|---|---|---|
| UART | USART6 (PG14/PG9) + RX/TX DMA | **USART1 TX=PA9, RX=PB7**, 115200 8N1, global IRQ açık | ST-LINK VCP'ye bağlı olan USART1; tek USB kablo yeter |
| TX DMA | DMA2 Stream6 (USART6) | **DMA2 Stream7**, Normal, Mem→Periph, FIFO kapalı | USART1_TX isteği |
| RX | DMA2 Stream1 | DMA yok, byte bazlı IT (`app_cmd_rx.c`) | düşük hızlı komut kanalı |
| PLL kaynağı | HSI 16 MHz (PLLM 8, PLLN 216) | **HSE 25 MHz** (PLLM 25, PLLN 432, P 2, Q 9) → 216 MHz | HSI ±%1 → ölçülen süreler de ±%1 kayardı |
| PI11 | GPIO_Input | **GPIO_EXTI11, yükselen + düşen kenar**, pull yok | buton olayı + bırakma sekmesini görmek |
| NVIC | — | USART1, DMA2_Stream7, EXTI15_10 = **5** | ISR'dan FreeRTOS API çağrısı için ≥ 5 olmalı |
| Cortex-M7 | varsayılan | I-Cache açık, **D-Cache kapalı** | DMA tutarlılığı |
| FreeRTOS | 15 KB heap | 24 KB heap, stack overflow check 2, malloc-failed hook, newlib reentrant | `App_RTOS_Init()` gereksinimleri |

Korunanlar: CMSIS_V2, TIM6 zaman tabanı, LSE, `CoupleFile=false`, CubeMX 6.12.0 / FW 1.17.4.

Üretimden sonra kontrol:

1. *Pinout*: PA9 = USART1_TX, PB7 = USART1_RX, PI11 = GPIO_EXTI11 (rising/falling).
2. *Clock Configuration*: PLL Source = HSE, HCLK = 216 MHz, kırmızı hata yok.
3. *Connectivity → USART1 → DMA Settings*: USART1_TX / DMA2 Stream 7.
4. *Middleware → FREERTOS → Tasks and Queues*: CubeMX'in `defaultTask`'ı (CubeMX en az bir görev
   tutar). ButtonTask ile aynı öncelikte (`osPriorityNormal`) her 1 ms uyanır; etkisi µs mertebesinde.
   İstenirse buradan önceliği `osPriorityIdle` yapılabilir.
5. Üretilen `Core/Inc/FreeRTOSConfig.h`'ta `#define xPortSysTickHandler SysTick_Handler`
   satırı bulunmalı (TIM zaman tabanında CubeMX bunu yazar; yoksa FreeRTOS tick'i çalışmaz).

## 4. Çalıştığını doğrulama

Kart resetlenince PC arayüzünün "Kart mesajları" listesinde:

```
BOOT: 216 MHz, DWT OK, fw 0.3
```

## 5. Doğrulama durumu

- **Gerçek proje derlemesi:** CubeMX ile üretilmiş UART_RTOS_ODEV1 projesi kendi
  `Debug/makefile`'ı ve CubeIDE 1.16.0 araç zinciriyle **uyarısız derlendi ve link edildi**
  (flash 47.7 KB, RAM 39 KB). Üretilen kodda HSE→216 MHz, USART1 + DMA2 Stream7 Ch4,
  PA9/PB7 AF7, PI11 her iki kenar, kesme öncelikleri 5 ve `xPortSysTickHandler → SysTick_Handler`
  doğrulandı; bağlanan ELF'te HAL callback'leri `app_hal_callbacks.o`'dan, hook'lar
  CubeMX'in `freertos.o`'sundan, `SysTick_Handler` FreeRTOS portundan geliyor.
  `freertos.c` CubeMX çıktısıyla birebir aynı.
- **Yığın:** derleyicinin `-fstack-usage` çıktısına göre en derin çağrı zinciri ≈ 1.1 KB
  (UartTxTask, dump), yığınlar 1536 / 1024 / 1536 B.
- **Yapılmadı:** kart üzerinde çalışma.
