# Traxcope kaydı ve güncel kod değerlendirmesi

İncelenen kayıt: `Bootcamp_Odev3_20261008_220606`.
Yerel başlangıç/bitiş: 08.10.2026 22:06:06–22:08:08.
Firmware veya SDK kaynaklarında değişiklik yapılmadı.

## 1. Buton zincirini kesen değişiklik

Önceki doğrulanmış derleme kopyasıyla güncel `Core` kaynakları karşılaştırıldı.
Tek fark `Core/Src/app_button.c:65` içindeki timeout değeridir:

```c
osMessageQueuePut(ButtonQueueHandle, &evt, 0u, 100u)
```

Bu fonksiyon gerçek EXTI ISR'ından çağrılıyor. Projedeki
`Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c:1660`
ISR yolunda `timeout != 0U` durumunu `osErrorParameter` olarak reddediyor.
`xQueueSendToBackFromISR()` çağrısına hiç ulaşılmıyor. Burada 100 tick bekleme
yapılmıyor; kuyruk doluluğuna bakılmadan parametre hatası dönüyor.

Sonuç zinciri:

1. Debounce, basılı pin ve START kontrolleri geçiliyor; buton ISR kaydı açılıyor.
2. Uygulama ring buffer'ında olay için yer ayrılıyor.
3. Kuyruğa gönderme parametre hatasıyla reddediliyor.
4. `RingBuffer_CloseDropIsr()` bu olayı DROP olarak kapatıyor.
5. `ButtonTask`a mesaj gitmiyor; BTN çerçevesi ve onun UART gönderimi oluşmuyor.

Uygulama bu dönüşü kuyruk doluluğuyla aynı DROP sayacına eklediği için
`btn_queue_drop_count` artması tek başına kuyruk gerçekten doldu demek değildir.

Veri bunu destekliyor: **47 buton ISR aralığı**, fakat `ButtonTask` için
**0 SWITCH_IN, 0 READY ve 0 çalışma aralığı** var. Buton ISR medyanı
**6,167 µs**, aralığı **6,074–6,273 µs**. Bu süre debounce kontrolünden sonraki
kayıtlı kısmın süresidir; fiziksel kesmenin tamamı veya butondan UART'a gecikme değildir.

22:03:01'de üretilmiş yerel Debug ELF disassembly'sinde de çağrı öncesi
`movs r3, #100` görülüyor. Kaynak, ELF ve kayıt davranışı aynı teşhisi destekliyor.
Kayıt, programlanan firmware'in SHA-256 kimliğini içermediğinden karta tam olarak
bu ELF'in yüklendiği ayrıca kanıtlanmış değildir.

Gerekli düzeltme bu ISR çağrısının son argümanını **`0u`** yapmaktır.
`APP_TX_QUEUE_SEND_TIMEOUT_MS=200u` ise farklıdır: görev bağlamındaki TxQueue
gönderimlerinde kullanılır ve ISR'daki yasaklı timeout ile karıştırılmamalıdır.
Güncel kod üzerinde mevcut `tests/button_trace/run.ps1` çalıştırıldığında
timeout'un sıfır olmasını denetleyen test başarısız oldu; test değiştirilmedi.

## 2. Kayıp veri ve ölçüm sınırı

| Ölçüm | Sonuç |
|---|---:|
| Ana kayıt parçası | 13 |
| Kaydedilmiş olay | 633.380 |
| Host tarafından oluşturulmuş çalışma aralığı | 438.001 |
| Çalışma aralıklarının zaman penceresi | 21,322742–142,033004 s |
| Bu pencerenin uzunluğu | 120,710261 s |
| `streamGaps` sayısı | 196 |
| Boşlukların toplamı | 35,865853 s |
| Zaman penceresine oranı | %29,7123 |
| Boşluk süresi medyanı | 182,225 ms |
| Boşluk süresi en az / en çok | 179,078 / 196,018 ms |

Kayıt zaman ekseninde ilk kontrol olayı 0'dadır; gerçek çalışma aralıkları
21,322742 s'de başlar. Baştaki yaklaşık 21 saniye görev çalışma ölçümüne
dahil edilmedi. Zaman ekseninin 142 saniyeye ulaşması, 142 saniyelik kesintisiz
kayıt bulunduğu anlamına gelmez.

SDK'nın SESSION_GAP açıklaması, ring buffer taşması sonrasında üretimin
duraklatılması, tamponun boşaltılması ve durum snapshot'ıyla yeniden
başlatılmasını tanımlar. Buradaki tekrarlayan yaklaşık 182 ms boşluklar bu
mekanizmayla uyumludur. Ancak kaydedilmiş `streamGaps` listesi resume-reason
alanını taşımıyor; taşıma doygunluğu veya başka bir nedeni tek başına kesin
olarak ayırmıyorum. 1 ms kontrol görevi periyodu, UART aktarım kapasitesini artırmaz.

Boşluk sırasında uygulama çalışmaya devam edebilir. Bu nedenle kayıtta görülen
47 basış toplam fiziksel basış sayısı değildir; tüm kayıt için maksimum
gecikme, toplam DROP sayısı veya deadline başarısı bu dosyadan kesinleştirilemez.

## 3. TelemetryTask ve UartTxTask

Kesintisiz izlenen bölgelerde, SysTick tarafından bölünmüş TelemetryTask
çalışma dilimleri birleştirilip görev CPU süresi hesaplandı. Diğer bitiş
türleriyle veya veri boşluğuyla kesilen parçalar tam çalışma diye birleştirilmedi.
Bu yöntemle 924 tamamlanmış çalışma grubu elde edildi:

| Gözlenen grup | Sayı | CPU süresi medyanı | En az / en çok |
|---|---:|---:|---:|
| Kısa çalışma | 549 | 120,268 µs | 118,629–127,852 µs |
| Yaklaşık 2 ms yük içeren çalışma | 375 | 2.115,828 µs | 2.114,028–2.125,750 µs |

İkinci grup kayıt ekseninde yaklaşık 27,165–38,147 s arasında görülüyor.
Bu, yaklaşık 2 ms ek CPU yüküyle uyumludur; kayıtta CFG metni veya
`load_iter` değeri olmadığından gönderilmiş komutu kesin olarak söylemiyorum.
Kayıt tek, sabit bir senaryo gibi değerlendirilmemelidir: kesintisiz gözlenen
TelemetryTask başlangıçları arasında yaklaşık **10, 20, 50 ve 200 ms**
aralıklar bulunuyor. Kaynak da CFG ile period/load değişimine izin veriyor.

`UartTxTask` için 5.394 SWITCH_IN olayı ve 5.400 çalışma dilimi görülüyor.
Çalışma dilimi medyanı **33,598 µs**, en uzunu **151,996 µs**.
Bu değer UART'ın tüm aktarım süresi değildir: görev DMA'yı başlatıp semaphore
beklerken CPU kullanmaz; gerçek bitiş zamanını USART1 TX callback'i kaydeder.
USART1 ISR kaydı kapalı olduğu için bu trace'ten t0→t4 buton deadline ölçümü
çıkarılamaz. Bu ölçüm için uygulamanın REC/STAT DUMP çıktısı gerekir.

## 4. Kontrol görevi ve görünürlük

TraxCtrl SWITCH_IN aralıklarının medyanı **0,999972 ms**; veri 1 ms ayarıyla
uyumlu. Araya giren görevler, kesmeler ve aynı tick içindeki yeniden başlatmalar
nedeniyle her SWITCH_IN aralığının tam 1 ms olması beklenmez.

Yalnızca kaydedilmiş çalışma aralıklarının toplamında görülen dağılım:

| Varlık | Gözlenen süre payı |
|---|---:|
| Idle | %94,8622 |
| TraxCtrl | %2,4001 |
| TelemetryTask | %1,0158 |
| SysTick | %0,9068 |
| Scheduler | %0,5984 |
| UartTxTask | %0,2164 |
| Buton ISR | %0,0003 |

Bu tablo tüm kayıt için gerçek CPU yükü değildir. Payda yalnızca
**84,736452 s** kayıtlı aralıktır; veri boşlukları dahil değildir. Kayıtlanmayan
USART/DMA/TIM6 kesmeleri de preempt ettikleri task/Idle süresinin içine
yazılabilir. Farklı yük senaryoları ve sistemin START/STOP beklemeleri aynı
kayıtta bulunduğundan bu yüzdeler tek deneyin sonucu olarak kullanılmamalıdır.

Metadata'da üç uygulama görevi, TraxCtrl, Tmr Svc ve Idle var. ISR listesinde
yalnızca `Button_EXTI15_10` ve SysTick bulunuyor. `defaultTask`, USART1/6, RCC
ve DMA ISR varlıkları yok. Tmr Svc metadata'da var fakat bu kayıtta çalışma
aralığı yok; metadata'da bulunmak çalıştığı anlamına gelmez.

## Dosyalar ve doğrulama

- `analyze.py`: ana TRXR v3 parçalarını okur; orijinal kayda yazmaz.
- `summary.json`: tüm istatistikler, parça boyutları ve dosya SHA-256 değerleri.
- `telemetry_runs.csv`: birleştirilmiş TelemetryTask çalışmaları.
- `button_isr.csv`: görülen 47 buton ISR aralığı.
- `button_disassembly.txt`: mevcut yerel Debug ELF'te timeout=100 kanıtı.
- `current_button_test.log`: güncel kodda mevcut buton testinin başarısız sonucu.

13 parçanın boyutu, bölüm sınırları, record stride değerleri ve 1–633.380
global event ID dizisi kontrol edildi. Global ID devamlılığı, probe tarafındaki
veri kaybını ortadan kaldırmaz. JSON'daki `digest` alanları dosyanın ham SHA-256
değerleriyle eşleşmiyor; bu alanın hesaplama sözleşmesi bilinmediğinden bu
farka dayanarak dosya bozukluğu veya doğrulanmış hash bütünlüğü iddiasında bulunmuyorum.

Firmware ve TraxProbe SDK kodları bu inceleme sırasında değiştirilmedi.
