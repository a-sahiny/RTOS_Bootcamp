# USART6 üzerinden Traxcope

TraxProbe 2.0.0, STM32F746 / FreeRTOS 10.2.0 projesine eklendi. SDK kaynağı:
`C:\Program Files\Embedya\Traxcope\sdk\TraxProbe`.
Gerekli Cortex-M ve FreeRTOS bileşenlerinin yerel kopyası
[`Middlewares/Third_Party/TraxProbe`](Middlewares/Third_Party/TraxProbe) içindedir.
Kopyalanan 96 SDK dosyası, kurulu SDK ile byte düzeyinde aynıdır.
Lisans ve NOTICE dosyaları bu klasörde korunmuştur.

## Kullanım ve SystemView'e dönüş

[`Core/Inc/app_trace_config.h`](Core/Inc/app_trace_config.h) varsayılan olarak:

```c
#define APP_TRACE_BACKEND APP_TRACE_TRAXCOPE
```

SystemView'e dönmek için yalnızca bu satırı değiştirin:

```c
#define APP_TRACE_BACKEND APP_TRACE_SYSTEMVIEW
```

Her seçim değişikliğinden sonra CubeIDE'de **Refresh → Clean → Build** yapıp
yeni firmware'i karta yükleyin. İki izleyici aynı anda çalıştırılmaz; USART6 ve
FreeRTOS trace hook'larının sahibi seçilen izleyicidir. SystemView bağlantı
adımları [SYSTEMVIEW.md](SYSTEMVIEW.md) dosyasındadır.

## Traxcope bağlantısı

| Kart | 3,3 V USB–UART adaptörü |
|---|---|
| PC6 / USART6_TX | RX |
| PC7 / USART6_RX | TX |
| GND | GND |

1. Traxcope seçili firmware'i karta yükleyip kartı çalıştırın.
2. Traxcope'ta UART/Serial bağlantısı için USB–UART adaptörünün COM portunu seçin.
3. **921600 baud, 8 veri biti, parity yok, 1 stop biti, flow control yok** kullanın.
4. Bağlanıp izleme kaydını başlatın. İki yönlü bağlantı gerekir: Traxcope'un
   ikili protokol komutlarını USART6 RX alır; olaylar USART6 TX'ten gider.
5. Uygulamanın mevcut arayüzünü **USART1'in ayrı COM portunda** kullanın.
   Uygulamaya `CFG + START` gönderip butona basın. Bu metin komutlarını
   Traxcope portuna göndermeyin.

Metadata FLASH'tadır ve oturum başlangıcında kablo üzerinden gönderilir.
Traxcope bağlantısını başlatmak için host'a ELF yüklemek zorunlu değildir.
Kaydı başlatmak uygulamadaki `g_running` durumunu değiştirmez; uygulama hâlâ
USART1'den gelen `START` komutuyla çalışır.

## SDK sözleşmesine göre yapılandırma

| Ayar | Bu proje | Kaynak / gerekçe |
|---|---|---|
| Görevi oluşturan | `trax_init()` | SDK'nın `trax_task_init()` çağrısı; ikinci bir görev oluşturulmaz |
| Kontrol görevi | `TraxCtrl`, öncelik **1** | SDK FreeRTOS örneğindeki IDLE üzeri öncelik |
| Stack | **512 kelime = 2048 bayt** | SDK örneğinin metadata başlangıç yolu için önerisi |
| Periyot | **1 ms** | Kullanıcının tercihi; SDK varsayılanı 10 ms |
| Zaman kaynağı | SysTick, 216 MHz / 1000 Hz | SDK'nın `TICK_TIMER` modu; HAL zaman tabanı TIM6 |
| FreeRTOS sürümü | **10.2.0** | Gerçek `task.h` sürümü; dosya başlığına göre seçilmedi |
| Çıkış olay tamponu | **16 KB** | FreeRTOS SDK örneğindeki 4096 adet 32-bit kelime |
| USART6 TX DMA kopyası | **8 KB** | Tek metadata karesi de aktarılabilsin; normal paket hedefi 1024 bayt |
| USART6 RX | 128 bayt normal DMA + 1024 bayt yazılım kuyruğu | Komut çözümleme ISR dışında, `TraxCtrl` içinde |
| Metadata | `TRAX_META_IN_FLASH` | SDK'nın özel UART transport örneği |

Başlıca dayanaklar:

- [FreeRTOS örnek ayarları](Middlewares/Third_Party/TraxProbe/config/templates/trax_config.cortexm_freertos_rtt.h)
- [Özel UART transport örneği](Middlewares/Third_Party/TraxProbe/config/templates/trax_config.cortexm_uart_custom.h)
- [Transport fonksiyon sözleşmesi](Middlewares/Third_Party/TraxProbe/inc/trax_transport.h)
- [FreeRTOS entegrasyon açıklaması](Middlewares/Third_Party/TraxProbe/os/FreeRTOS/README.md)
- [Gerçek linker dosyası](Middlewares/Third_Party/TraxProbe/linker/trax_probe.ld)

SDK ana kılavuzunda bazı eski dosya adları bulunuyor. Proje, kurulu paketteki
gerçek `inc/` dizinini ve `trax_probe.ld` dosyasını kullanır.

## Kodun çalışma sırası

1. `main()` saatleri, UART/DMA'yı ve uygulamanın DWT sayacını hazırlar.
2. `osKernelInitialize()` ardından, uygulama task ve kuyrukları oluşturulmadan
   `App_Trace_Init()` çağrılır. Traxcope seçiminde bu çağrı `trax_init()`e gider.
3. SDK tablolarını ve transport'u başlatır, statik `TraxCtrl` görevini oluşturur.
4. `App_RTOS_Init()` uygulamanın kuyruklarını ve görevlerini oluşturur.
   FreeRTOS hook'ları bunları SDK tablolarına kaydeder.
5. Scheduler başladıktan sonra `TraxCtrl`, SDK'nın kendi döngüsünde
   `trax_process()` çalıştırır ve `vTaskDelayUntil()` ile 1 ms periyot izler.
6. USART6 RX callback'i baytları kuyruğa alır. Komut çözümleme ve oturum
   başlatma/durdurma işlemleri SDK görevinde yapılır.
7. TX, SDK verisini ayrı ve cache-line hizalı tampona kopyalar; DMA devam ederken
   bu tampon tekrar kullanılmaz. Transport `write()` çağrısı **tamamını kabul
   eder veya 0 döndürür**. Meşgul UART, uygulama görevini bekletmez.

FreeRTOS hook'ları `FreeRTOSConfig.h` sonundan seçilir. TraxProbe tick hook'unu
SDK sağlar. Traxcope seçiminde CMSIS-RTOS2'nin `SysTick_Handler` sarmalayıcısı,
FreeRTOS'un `xPortSysTickHandler()` fonksiyonunu çağırır; çift handler oluşması
yapılandırmada engellenmiştir. FreeRTOS kernel kaynakları değiştirilmemiştir.

Uygulamanın eklediği tek ISR kaydı `Button_EXTI15_10`dur. Kayıt,
`Button_HandleEXTI()` içindeki debounce, basılı pin ve `g_running` kontrolleri
geçildikten sonra başlar. Mevcut debounce, son kenardan itibaren 30 ms sessizlik
arar; ilk kenar kabul edilebilir, bırakma kenarı da sessizlik süresini yeniler.
ISR içinde 30 ms bekleme yapılmaz. Reddedilen kenarlar kaydedilmez.

USART1/6, DMA, RCC ve TIM6 için uygulama ISR kayıtları kaldırılmıştır;
bu kesmeler normal çalışmaya devam eder. Kullanılmayan `defaultTask`, hem
`main.c` hem CubeMX ayarlarından kaldırılmıştır. Uygulama görevleri
`TelemetryTask`, `ButtonTask` ve `UartTxTask` olarak kaydedilir. SDK kaynaklarına veya
FreeRTOS hook'larına görünürlük filtresi eklenmemiştir: `TraxCtrl`, `Tmr Svc`,
`IDLE` ve SDK'nın otomatik SysTick kaydı görünmeye devam edebilir.
SystemView seçeneğinde de yalnızca kabul edilen buton olayı kaydedilir.

## Değişen dosyalar

| Dosya | İşlev |
|---|---|
| `Core/Inc/app_trace_config.h` | İzleyici seçimi |
| `Core/Inc/app_trace.h` | Başlatma ve UART callback yönlendirmesi |
| `Core/Inc/trax_config.h` | Projeye özel SDK ayarları |
| `Core/Inc/app_traxcope.h`, `Core/Src/app_traxcope.c` | USART6 custom transport ve DMA callback'leri |
| `Core/Inc/FreeRTOSConfig.h` | Seçilen izleyicinin hook'ları ve tick eşlemesi |
| `Core/Src/main.c`, `UART_RTOS_ODEV1.ioc` | Başlatma sırası ve kullanılmayan defaultTask kaldırılması |
| `Core/Src/app_hal_callbacks.c` | USART6 callback'lerini seçilen izleyiciye yönlendirme |
| `Core/Src/app_button.c` | Debounce kontrolünden sonra seçilen izleyiciyle buton ISR kaydı |
| `Core/Src/app_systemview.c`, `SEGGER_SYSVIEW_FreeRTOS.c` | SystemView seçeneğinde derleme |
| `.cproject` | Debug/Release include yolu ve ikinci `-T` linker girdisi |

CubeMX yeniden kod üretirse `.cproject` içindeki TraxProbe include/linker
ayarlarını ve `FreeRTOSConfig.h` içindeki `configUSE_TICK_HOOK` korumasını
kontrol edin. CubeMX'te ayrı tick-hook stub'ı ürettirmeyin; SDK bu fonksiyonu sağlar.

## Doğrulama ve sınırlar

08.10.2026 tarihinde STM32CubeIDE 1.16.0 / ARM GCC 12.3.1 ile ayrı geçici proje
kopyasında iki izleyicinin Debug ve Release derlemeleri tamamlandı.

| Seçim | Yapılandırma | FLASH (`text + data`) | RAM (`data + bss`) |
|---|---|---:|---:|
| Traxcope | Debug | 98480 bayt | 70356 bayt |
| Traxcope | Release | 49652 bayt | 70364 bayt |
| SystemView | Debug | 60772 bayt | 49792 bayt |
| SystemView | Release | 36092 bayt | 49760 bayt |

Bu değerler GNU `size` çıktısından alınmıştır; çalışma sırasındaki stack
tepe kullanımı ölçümü değildir. `configTOTAL_HEAP_SIZE = 25600` korunmuştur.
SDK'nın kontrol görevi ve tamponları ek statik RAM kullanır; yaklaşık 20 KB
fark, mevcut görevlerin stack boyutlarını değiştirmez.

- Traxcope UART testleri geçti: DMA tampon ömrü, meşgul/hatalı TX başlangıcı,
  parçalı komutlar, RX kuyruğu sarımı/taşması, UART/DMA hata toparlama ve cache hizalaması.
- Buton ISR testi geçti: debounce sınırı, bırakma kenarı, START koşulu,
  kuyruk doluluğu, PendSV ve tick taşması doğrulandı.
- Mevcut SystemView UART testleri ve USART1 RX testleri geçti.
- Mevcut `systemview_states` testi 72. satırdaki Blocked olayı kontrolünde
  başarısız. Entegrasyon öncesi SEGGER adapter'ıyla aynı hata yeniden üretildi.
  Mevcut kernelde bu testin beklediği ek liste-geçiş hook çağrıları bulunmuyor;
  bu entegrasyon kapsamında kernel değiştirilmedi.
- Traxcope Release derlemesinde SDK'nın değiştirilmemiş `trax_core.c` dosyasından
  `iov[0].len` ve `iov[0].p_base` için iki `-Wmaybe-uninitialized` uyarısı geliyor.
  Uyarılar bastırılmadı. Debug derlemesinde uyarı yok.
- Kart programlanmadı; fiziksel Traxcope bağlantısı, uzun süreli kayıt ve gerçek
  stack high-water mark henüz doğrulanmadı.

`TraxCtrl` düşük öncelikli olduğu için yüksek CPU yükünde gecikebilir. Ayrıca
921600 baud / 8N1 hattının teorik üst sınırı 92160 bayt/s'dir; 1 ms periyot
bu sınırı artırmaz. Sürekli yoğun olay üretiminde SDK'nın tampon taşması ve
`SESSION_GAP` göstergeleri izlenmelidir. Kesintisiz kayıt garantisi verilmez.

Hazır [Debug ELF](../../output/traxcope/Debug/UART_RTOS_ODEV1.elf),
[Release ELF](../../output/traxcope/Release/UART_RTOS_ODEV1.elf) ve
[doğrulama kayıtları](../../output/traxcope/verification) çıktı klasöründedir.
Host testleri depo kökünden `tests/traxcope/run.ps1` ve
`tests/button_trace/run.ps1` ile çalıştırılır.
