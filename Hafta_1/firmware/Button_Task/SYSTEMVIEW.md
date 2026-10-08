# USART6 üzerinden SystemView

Bu proje STM32F746G-DISCO üzerinde FreeRTOS 10.2.0 olaylarını USART6 üzerinden
SystemView'a gönderir. ST-LINK firmware dönüşümü gerekmez. USART1'in mevcut
921600 baud komut/telemetri iletişimi korunur.

## Bağlantı ve ilk kayıt

3,3 V seviyeli USB–UART adaptörünü şu şekilde bağlayın:

| Kart | USB–UART adaptörü |
|---|---|
| PC6 / USART6_TX | RX |
| PC7 / USART6_RX | TX |
| GND | GND |

Kartın ST-LINK USB bağlantısı güç, programlama/debug ve mevcut USART1 arayüzü
için kullanılır. USART6, ST-LINK'in sanal COM portuna bağlı değildir; SystemView'da
USB–UART adaptörünün COM portunu seçin. Hem TX hem RX gereklidir; PC de kayıt
başlatma/durdurma komutlarını gönderir.

1. CubeIDE'de projeyi yenileyin (Refresh), Clean ve Build yapın, firmware'i karta yükleyin.
2. Kartı resetleyip çalışır durumda bırakın; breakpoint üzerinde durmamalı.
3. SystemView'da **Target → Recorder Configuration → UART** seçin.
4. USB–UART adaptörünün COM portunu ve **921600 baud, 8N1, flow control yok** ayarını kullanın.
5. **F5 / Start Recording** ile kaydı başlatın.
6. Mevcut uygulama arayüzünü USART1'in ayrı COM portunda açın. Düşük yükte
   `CFG + START` gönderin ve butona basın. Sonra yüksek yükle karşılaştırın.
7. Kaydı durdurup `.SVDat` olarak saklayın.

SystemView kaynaklarının uyumluluk sürümü 3.32'dir; bu sürümü veya daha yenisini
destekleyen masaüstü uygulamasını kullanın. Aynı UART oturumunda Stop/Start
desteklenir. COM bağlantısını tamamen kapatıp yeniden açarken kartı resetleyin;
kullanılan SEGGER örnek recorder'ı her resetten sonra bir bağlantı el sıkışması yapar.

## Kaydın gösterdikleri

- `defaultTask`, `UartTxTask`, `ButtonTask`, `TelemetryTask`, timer task ve Idle durumu.
- Task çalışma/hazır/bekleme/suspend geçişleri.
- Veri kuyruklarının oluşturma, gönderme, alma ve silme olayları.
- Yalnızca buton kesmesi: `Button_EXTI15_10`.

SysTick, TIM6, USART1/6 ve bütün DMA stream kesmelerinin kayıtları kapalıdır.
Gecikme, notify, suspend/resume gibi fonksiyon çağrıları ayrıca listelenmez;
bunların oluşturduğu task durum geçişleri kaydedilir. Semaphore/mutex işlemleri
FreeRTOS içinde queue yapısını kullansa da veri kuyruğu olaylarından süzülür.
Task adları, zaman bilgisi ve kayıt başlatma/durdurma gibi gerekli protokol
bilgileri korunur. Bu seçim kaynakta uygulanır; PC'de ayrıca filtre ayarlamak gerekmez.

`START` öncesinde buton uygulama tarafından kabul edilmez. Buton kesmesinden
`ButtonTask`'ın çalışmasına kadar olan aralığı Timeline üzerinde inceleyin.
UART/DMA devam ederken task'ın beklemesi, CPU'nun o süre boyunca meşgul olduğu
anlamına gelmez. `CPU Load` ve `Events` pencerelerini birlikte kullanın.

Gizlenen kesmeler çalışmaya devam eder. Ayrı ISR olarak ölçülmedikleri için
süreleri o anda görünen task/Idle bağlamına dahil olabilir; bu sade görünümde
CPU yüzdeleri bütün kesmeleri ayrı ölçen bir kayıtla aynı anlamı taşımaz.

## Uygulama düzeni

### Ready, Running ve Suspended durumlarını görmek

Yeni firmware'i karta yükledikten sonra SystemView'da yeni bir kayıt başlatın.
**Timeline** üzerinde task satırlarını, **Events** üzerinde durum olaylarını
inceleyin. **Context Statistics** seçilen task'ın çalışma ve bekleme sürelerini gösterir.

| FreeRTOS durumu | Kaydedilen olay | Nasıl okunur? |
|---|---|---|
| Ready | Task Start Ready | Task çalışabilir; CPU sırasını bekler. |
| Running | Task Start Exec | Scheduler bu task'ı çalıştırmıştır. Başka task/kesme başlayana kadar izlenir. |
| Blocked | Task Stop Ready, neden 4 | Gecikme, kuyruk, semaphore veya bildirim bekler. Standart SystemView FreeRTOS tanımındaki adı **Delayed**'dir. |
| Suspended | Task Stop Ready, neden 27 | `osThreadSuspend()` / `vTaskSuspend()` ile açıkça askıya alınmıştır. |

`osWaitForever` ile kuyruk beklemek **Blocked** durumudur. FreeRTOS bunu kendi
`xSuspendedTaskList` listesinde tutsa da SystemView'a gerçek suspend olarak gönderilmez.
`defaultTask` açıkça kendini askıya aldığı için **Suspended** görünmelidir.
Yüksek öncelikli TelemetryTask çalışırken hazır olan ButtonTask ise **Ready** kalır;
çalışma sırası geldiğinde **Running** olur.

Kayıt başlangıcında `SYSVIEW_SendTaskStates()` mevcut durumları bir kez gönderir.
Böylece kayıttan önce askıya alınmış `defaultTask` da görülür. Durumlar çekirdek
hook'larıyla güncellenir; periyodik tarama, polling veya ek task yoktur.
Adaptör, en fazla 8 task için toplam **32 bayt** ek sabit RAM kullanır.
Start callback'i USART6 kesmesinde çalışabildiğinden RTOS listelerini dolaşmaz;
hook'ların tuttuğu durumları kullanır. Task listesi bilgilerini yenilemek yeni bir
durum geçişi üretmez; Stop/Start ise yeni başlangıç durumlarını gönderir.

Önceki `.SVDat` dosyalarına bu eksik olaylar sonradan eklenemez; yeni firmware
ile yeni kayıt gerekir. CubeMX FreeRTOS kaynaklarını yeniden oluşturursa aşağıdaki
durum testini çalıştırın; kaybolan bekleme hook'ları testi başarısız yapar.

```powershell
./tests/systemview_states/run.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe
```

### Kaynak yerleşimi

- [app_systemview.c](Core/Src/app_systemview.c): SystemView yapılandırması,
  UART recorder el sıkışması/komut aktarımı, 256 baytlık TX DMA parçaları ve
  32 baytlık RX DMA tamponu.
- [SEGGER_SYSVIEW_Conf.h](Core/Inc/SEGGER_SYSVIEW_Conf.h): 8192 bayt kayıt tamponu.
- [SEGGER_RTT_Conf.h](Core/Inc/SEGGER_RTT_Conf.h): kanal ve iç içe çağrılara uygun
  kesme kilidi. RTT burada RAM tamponudur; veriyi PC'ye USART6 taşır.
- [main.c](Core/Src/main.c): DWT sayacı SystemView'dan ve task oluşturmadan önce
  bir kez başlatılır. `defaultTask` kendini askıya alır; periyodik uyanma veya
  SystemView kontrolü yapmaz. Yeni task yoktur. Mevcut stack'i 1024 bayt,
  `configTOTAL_HEAP_SIZE` değeri 25600 bayttır.
- [app_hal_callbacks.c](Core/Src/app_hal_callbacks.c): USART6 callback'leri ayrı
  yönlendirilir; USART1'in kuyruk ve semaphore akışı korunur.
- FreeRTOS `tasks.c`, `include/FreeRTOS.h` ve
  `portable/GCC/ARM_CM7/r0p1/port.c`: V10 adaptörünün gereken trace noktaları.

USART6 alımı `HAL_UARTEx_ReceiveToIdle_DMA()` ile DMA2_Stream1 / Channel5 üzerinde
Normal modda yapılır. UART IDLE olayı kısa komutları tampon dolmadan teslim eder;
32 bayt dolarsa DMA tamamlanma olayı kullanılır. `HAL_UARTEx_RxEventCallback()`
alınan veriyi SystemView adaptörüne aktarır. Yarım tampon kesmesi kapalıdır.
Veri kopyalandıktan sonra DMA yeniden başlatılır ve gelen komutlar işlenir.
Bir DMA parçasındaki komutların sığması için RTT komut tamponu 33 bayttır
(32 bayt kullanılabilir). RX tamponu cache satırına hizalıdır; D-cache etkinse
DMA öncesi ve sonrası gereken cache işlemleri yapılır.

SystemView taşıması tamamen olaylarla ilerler: RX/IDLE callback'i el sıkışmasını
ve Start/Stop komutlarını işler; yeni trace olayı veya TX tamamlanma callback'i
bekleyen verinin gönderimini başlatır. Periyodik polling fonksiyonu yoktur.
DMA başlatma başarısız olursa ilgili yön durdurulup aynı callback içinde bir kez
yeniden denenir. Bu deneme de başarısızsa `Error_Handler()` çalışır; sonsuz
tekrar veya zamanlayıcıyla kontrol yapılmaz. UART alım hatası RX'i yeniden
başlatır; HAL'in iki yönü de sonlandırdığı DMA hatasında önce iki DMA durdurulur.

USART1 komut alımı DMA2_Stream2 / Channel4 üzerinde 256 baytlık **Circular**
DMA tamponu kullanır. IDLE, yarım tampon ve tam tampon olayları yeni baytları
`CmdRx_RxEvent()` üzerinden toplar; tamamlanan satırlar yine `CmdQueue`'ya gider.
Gecikmiş veya yinelenen callback'lerde veri tekrar işlenmesin diye DMA'nın güncel
konumu okunur. Tampon olaylar arasında tamamen turlarsa veri kaybolabilir;
921600 baud'da 256 bayt yaklaşık 2,78 ms sürer. Normal alımda DMA durdurulmaz.
Her iki UART'ın alımı da bayt başına `HAL_UART_Receive_IT()` kullanmaz.

CMSIS-RTOS2 bir kesmede birden fazla `portYIELD_FROM_ISR` çağırabildiğinden,
buton ISR çıkışı handler sonunda bir kez kaydedilir. `portmacro.h` bu amaçla
değiştirilmemiştir. CM7 portundaki SysTick hook'ları bu kayıt seçiminde boş makrolardır;
tick'in uyandırdığı task'ların Ready ve Running olayları yine kaydedilir.

Gönderici UART'ı veya semaphore'u beklemez. DMA meşgulse veriler tamponda kalır;
HAL başlangıç isteğini reddederse ayrılmış parça callback içindeki tek toparlama
denemesinde korunur.
Üretim hızı hat kapasitesini aşarsa SystemView olayları düşebilir; uygulama bloke
edilmez. SystemView'daki **Overflow** olayları bu kaydı eksiksiz ölçüm olarak
kullanmamak gerektiğini gösterir. USART6'nın başlangıçtaki 115200 baud değeri,
trace trafiğine daha fazla alan sağlamak için 921600 olarak değiştirilmiştir.

CubeMX ayarlarında her iki UART 921600 baud, USART1 RX Circular DMA,
USART6 RX Normal DMA ve ilgili UART/DMA IRQ öncelikleri 5 olarak saklanmıştır. CubeMX yeniden üretiminden sonra include yollarını ve USER CODE
çağrılarını kontrol edin. FreeRTOS middleware'ini değiştirirseniz çekirdek trace
değişikliklerini yeni sürüme uyarlayın; farklı sürümün patch'ini doğrudan uygulamayın.

## Doğrulama

Donanım gerektirmeyen taşıma testleri gerçek SEGGER SystemView, RTT ve recorder
kaynaklarını sahte UART/DMA katmanı üzerinde çalıştırır:

```powershell
./tests/systemview/run.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe
./tests/systemview_states/run.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe
./tests/uart_rx/run.ps1 -Compiler C:/msys64/ucrt64/bin/gcc.exe
```

Komutu `Hafta_1` klasöründen çalıştırın. Testler el sıkışmasını, HAL_BUSY sonrası
veri korumasını, Start/Stop komutlarını, DMA sırasında tamponun değişmemesini,
kilit durumunun korunmasını, kısa/tam RX DMA parçalarını, komut gruplarını,
RX yeniden başlatma tekrarlarını, cache işlemlerini, DMA hata toparlanmasını
ve dolu kayıt tamponundan sonra devam edilebilmesini denetler.
USART1 testleri ayrıca parçalı komutları, dairesel tampon sınırlarını, yinelenen
callback olaylarını ve hata sonrası komut alımını iki kuyruk düzeninde denetler.
Durum testleri gerçek FreeRTOS `tasks.c`/`list.c` ve SEGGER adaptörünü kullanır;
CPU portu ve olay çıkışı taklittir. Gecikme/uyanma, sonsuz kuyruk beklemesi,
suspend/resume, öncelik değişimi, tick taşması ve başlangıç durumları denetlenir.
CM7 SysTick çalışırken kesme/fonksiyon kaydı çıkmadığı, task geçişlerinin kaldığı,
gerçek veri kuyruğu işlemlerinin kaydedildiği ve semaphore işlemlerinin süzüldüğü
ayrıca test edilir.
Elektrik bağlantısı, gerçek baud uyumu ve donanım
zamanlaması için yukarıdaki kart testi ayrıca gereklidir.

## Kaynaklar

- [SEGGER UART Recorder](https://kb.segger.com/Use_SystemView_UART_Recorder)
- [SEGGER FreeRTOS entegrasyonu](https://kb.segger.com/FreeRTOS_with_SystemView)
- [Eklenen kaynakların kökeni ve yerel uyarlamalar](Middlewares/Third_Party/SEGGER/UPSTREAM.md)
