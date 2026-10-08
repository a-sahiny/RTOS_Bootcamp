# Son kayıt ve assert değerlendirmesi

İncelenen kayıt: `Bootcamp_Odev3_20261008_230215`.
Başlangıç/bitiş: 08.10.2026 23:02:15–23:03:33.
Firmware ve TraxProbe SDK kaynakları değiştirilmedi.

## Assert'in nedeni

Paylaşılan debugger Call Stack:

```text
trax_task_func()                         trax_freertos_task.c:178
  → vTaskDelayUntil()                    tasks.c:1288
    → EXTI15_10_IRQHandler()             [kesme araya giriyor]
      → HAL_GPIO_EXTI_IRQHandler()
        → HAL_GPIO_EXTI_Callback()
          → Button_HandleEXTI()          app_button.c:65
            → osMessageQueuePut()       cmsis_os2.c:1679
              → xQueueGenericSend()     queue.c:758 [assert]
```

`vTaskDelayUntil()` önce `vTaskSuspendAll()` çağırır (`tasks.c:1258`),
uyanma zamanını/gecikme listesini düzenler, sonra `xTaskResumeAll()` çağırır
(`tasks.c:1314`). Bu kısa bölümde scheduler askıdadır; tüm kesmeler kapatılmış
değildir. Buton kesmesi bu bölüme girebilir.

Güncel `cmsis_os2.c:1660` koşulu:

```c
if (IS_IRQ() && 0) {
```

`&& 0` yüzünden ISR yolu hiçbir zaman seçilmez. Böylece kesme bağlamında
`xQueueSendToBackFromISR()` yerine görev API'si `xQueueSendToBack()` seçilir;
bu makro `xQueueGenericSend()`e gider. İşlemci hâlâ ISR bağlamındadır.

Güncel `app_button.c:65` çağrısı:

```c
osMessageQueuePut(ButtonQueueHandle, &evt, 0u, 100000u)
```

Burada bekleme süresi sıfır değildir. Scheduler askıda olduğu anda bu çağrıya
girildiğinden `queue.c:758` assert'i tetiklenir. Kontrol, kuyruğun doluluğuna
bakılmadan önce yapılır; boş kuyruk da bu hatayı engellemez. `100000` tick,
mevcut 1 kHz tick ayarında 100 saniyelik bekleme talebidir; ISR bunu yapamaz.

Projede `configASSERT` kesmeleri kapatıp sonsuz döngüye girer. Kullanıcının
breakpoint olmadığını belirtmesi bu durmayla uyumludur.

Bu, TraxCtrl'ın hatalı şekilde scheduler'ı askıda bırakması değildir:
`vTaskDelayUntil()` normal FreeRTOS iç işlemidir. Hata, kesme içinde görev
API'sinin seçilmesi ve ona sıfırdan farklı timeout verilmesidir. Önceki kayıtta
aynı API timeout'u parametre hatasıyla reddediyordu; şimdi bu koruma atlanmış.

Güvenli düzeltme iki değişikliği birlikte geri almaktır:

1. CMSIS koşulunu `if (IS_IRQ())` haline döndürmek.
2. ISR'daki `osMessageQueuePut` timeout'unu `0u` yapmak.

Sadece timeout'u sıfırlamak, görev API'sinin ISR'da kullanılmasını düzeltmez.
Sadece CMSIS koşulunu düzeltmek de `100000u` talebini parametre hatasıyla
reddettirir. Timeout gerekiyorsa bekleme ButtonTask gibi görev bağlamına
taşınmalıdır. TraxProbe SDK'sında değişiklik gerekmez.

## Son kayıttaki davranış

| Ölçüm | Sonuç |
|---|---:|
| Ana parça / kayıtlı olay | 2 / 62.123 |
| Çalışma aralıklarının zaman penceresi | 302,658706–314,512003 s |
| Pencere uzunluğu | 11,853297 s |
| Veri boşluğu | 35 |
| Boşlukların toplamı | 6,416726 s |
| Pencereye oranı | %54,1345 |
| Buton ISR aralığı | 28 |
| ButtonTask SWITCH_IN / READY | 32 / 28 |
| UartTxTask SWITCH_IN | 310 |
| TelemetryTask SWITCH_IN | 0 |
| defaultTask SWITCH_IN | 5.425 |
| TraxCtrl SWITCH_IN aralığı medyanı | 0,999958 ms |

Önceki kayıttan farklı olarak ButtonTask şimdi uyanıyor. Bu, görev API'sinin
ISR içinde kullanılmasıyla bazı gönderimlerin gerçekleşebildiğini gösterir;
kullanımın geçerli olduğunu kanıtlamaz. Scheduler askıda olduğu kısa pencereye
denk gelen kesme assert'i tetikleyebilir; hata aralıklı görünebilir.

`defaultTask` hem güncel kaynakta hem bu kayıtta yeniden var ve 1 ms döngüsünde
çalışıyor. Bu ek görev trace trafiği üretir; assert'in doğrudan sebebi değildir.
Bu kayıtta görünür bir TelemetryTask çalışması bulunmadığından telemetry yükü
ve periyodu değerlendirilemez.

Kaydın son olayı 314,512003 s'deki SysTick girişidir. Assert çağrı zinciri
trace dosyasında tam olarak saklanmamıştır: assert'te kesmeler kapanınca
TraxCtrl çalışıp tamponu UART'tan boşaltamaz. Hatanın yerini kesinleştiren kanıt
paylaşılan Call Stack ve güncel kaynak kodudur. JSON'da stopReason yoktur;
host'un bitiş saati ile son olay zamanı aynı zaman tabanı değildir.

Veri boşlukları ölçüm penceresinin yarısından fazladır. Bu kayıtla tüm süre
için maksimum buton gecikmesi, toplam kayıp veya deadline başarısı
kesinleştirilemez. SDK ve USART taşıma davranışı bu hata düzeltildikten sonra
yeniden kesintisiz kayıtla ölçülmelidir.

`summary.json`, `button_isr.csv` ve `telemetry_runs.csv` analiz çıktılarıdır.
Tekrar üretmek için önceki analiz klasöründeki script'e giriş ve çıkış
klasörlerini verin:

```powershell
C:/msys64/ucrt64/bin/python.exe Traxcope/Analiz_Bootcamp_Odev3_20261008_220606/analyze.py Traxcope/Bootcamp_Odev3_20261008_230215 Traxcope/Analiz_Bootcamp_Odev3_20261008_230215
```

Parça boyutları, bölüm sınırları ve global event ID devamlılığı doğrulandı.
Global event ID devamlılığı probe tarafındaki veri kaybını dışlamaz. JSON
digest alanının hesaplama sözleşmesi bilinmediği için SHA-256 bütünlük
doğrulaması yapıldığı iddia edilmez; ham dosya hash'leri summary.json'dadır.
