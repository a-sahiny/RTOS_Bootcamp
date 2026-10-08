# Üç yeni SystemView kaydı — kodla doğrulanmış inceleme

İnceleme tarihi: 05 Ekim 2026. Hedef: STM32F746G-DISCO / FreeRTOS 10.2.0. Önce mevcut kaynak ve Debug ELF incelendi; ardından SVDat paketleri doğrudan çözümlendi. Firmware ve orijinal kayıtlar değiştirilmedi.

Ana bulgu: yük yükseldikçe ButtonTask’ın Ready beklemesi artıyor. En uzun bekleme yüksüzde 29,870 µs, 2 ms yükte 1.377,102 µs, 5 ms yükte 3.726,125 µs. Yüklü örneklerde gecikmenin büyük bölümü, ISR’dan sonra yüksek öncelikli TelemetryTask’ın devam etmesinden kaynaklanıyor. Bu bulgu aşağıdaki olay sıralarıyla doğrulandı.

## 1. Dosyalar bağımsız üç kayıt değil

Bayt karşılaştırması: 10ms_5msLoad dosyasının bütün olay akışı 10ms_NoLoad dosyasının başında; NoLoad akışının tamamı da 10ms_2msLoad dosyasının başında bulunuyor. Metin başlığındaki kayıt zamanı dışarıda bırakılarak ikili akış karşılaştırıldı. Dosyaları birleştirip toplamak aynı olayları tekrar sayardı.

| Dosya | Olay sayısı | İçerdiği oturumlar | Karşılaştırılan son oturum |
| --- | --- | --- | --- |
| 10ms_5msLoad.SVDat | 113674 | 1–6 | 6 |
| 10ms_NoLoad.SVDat | 172652 | 1–9 | 9 |
| 10ms_2msLoad.SVDat | 191946 | 1–10 | 10 |

Bu rapor dosya adlarını, her dosyanın son Start/Stop oturumuna etiket olarak uyguluyor: 6 = 5 ms yük, 9 = yüksüz, 10 = 2 ms yük. Yük etiketleri ayrıca ölçülen task süreleriyle uyumlu. Kayıt akışı CFG komutunun metnini/load_iter değerini içermediği için bu değerlerin doğrudan kayıttan okunduğu iddia edilmiyor.

Toplam 191.946 benzersiz olay ve 10 oturum var. Ana karşılaştırma 50.716 olaylık üç son oturuma dayanıyor. İlk oturumda Trace Stop yok; o oturum son görülen olayda kesilmiş kabul edildi ve ana karşılaştırmaya alınmadı.

| Oturum | Süre (s) | Telemetri kuyruğa yazma | Telemetry medyan (µs) | Buton ISR sayısı | Durum |
| --- | --- | --- | --- | --- | --- |
| 1 | 19,597 | 1960 | 157,537 | 76 | Dosya içindeki önceki oturum / Stop yok |
| 2 | 17,795 | 1501 | 157,523 | 114 | Dosya içindeki önceki oturum |
| 3 | 16,117 | 0 | — | 20 | Dosya içindeki önceki oturum |
| 4 | 18,066 | 1357 | 4775,380 | 20 | Dosya içindeki önceki oturum |
| 5 | 13,836 | 1384 | 3452,111 | 15 | Dosya içindeki önceki oturum |
| 6 | 12,079 | 1208 | 5158,722 | 16 | Ana karşılaştırma |
| 7 | 13,180 | 1318 | 2157,736 | 25 | Dosya içindeki önceki oturum |
| 8 | 18,213 | 1822 | 2157,782 | 21 | Dosya içindeki önceki oturum |
| 9 | 10,940 | 965 | 157,042 | 15 | Ana karşılaştırma |
| 10 | 13,521 | 1353 | 2157,766 | 20 | Ana karşılaştırma |

## 2. Güncel kodda doğrulanan yapı

| Alan | Kaynakta görülen durum | Analize etkisi |
| --- | --- | --- |
| Task öncelikleri | TelemetryTask 32 > ButtonTask 24 > UartTxTask 8 | Buton hazır olsa bile TelemetryTask çalışmaya devam edebilir. Bu değerler Task Info paketlerinde de aynı. |
| TX mimarisi | APP_TX_MODE_LARGE_FIFO; ortak TxQueue = 256 | BTN ve TEL aynı FIFO’ya giriyor; ayrı BTN kuyruğu seçili değil. |
| Diğer kuyruklar | ButtonQueue = 8; CmdQueue = 4 | Kapasiteler kaynak koddan; kayıtta doluluk sayacı yok. |
| TelemetryTask | osDelayUntil; 10 ms alt sınır; ardından load_iter döngüsü | Task süresi yalnız yapay yükü değil, çerçeve üretimi ve kuyruk işlemlerini de içerir. |
| ButtonTask | ButtonQueue üzerinde osWaitForever | Boş kuyrukta Blocked. vTaskSuspend ile askıya alınmış sayılmaz. |
| UartTxTask | TX DMA + TxDoneSem; boş TX kuyruğunda 20 tick bekleme | Kısa çalışma parçaları arasında bloklanma beklenen davranış. |
| defaultTask / Tmr Svc | defaultTask kendini suspend ediyor; timer görevi etkin | Son oturumlarda defaultTask Suspended, Tmr Svc Blocked; çalışma olayı yok. |
| Saat / seri port | 216 MHz; 1 kHz RTOS tick; USART1 ve USART6 = 921600 | Zaman hesabı Init paketindeki 216 MHz ile yapıldı. |
| Trace seçimi | Task durumları, veri kuyrukları, Idle ve buton ISR; diğer IRQ kayıtları kapalı | Gizlenen kesmeler CPU’da çalışır; yük yüzdelerinde ayrı bir ISR satırına ayrılmaz. |
| Derleme | Mevcut Debug make kuralı -O0 | Sonuçlar bu izlemeli çalışmaya aittir; Release veya izlemesiz performans sonucu değildir. |

Kaynaklar: Core/Inc/app_config.h; Core/Inc/FreeRTOSConfig.h; Core/Src/app_rtos.c; app_tasks.c; main.c; app_systemview.c; SEGGER_SYSVIEW_FreeRTOS.h/.c. İncelenen dosyaların SHA-256 değerleri ve kopyaları raporla birlikte saklandı. Debug ELF içindeki Button_HandleEXTI disassembly’si de aşağıdaki kayıt yerleşimini doğruluyor. Karta yüklü ELF’nin kimliği cihazdan okunmadı; kaynak/ELF/trace uyumluluğu bir firmware hash eşleşmesi değildir.

## 3. ISR ölçümünün gerçek sınırları

```text
EXTI15_10_IRQHandler
  → HAL_GPIO_EXTI_IRQHandler
    → HAL_GPIO_EXTI_Callback
      → Button_HandleEXTI
        → Timestamp_Now / HAL_GetTick / debounce / pin / g_running kontrolü
        → SEGGER_SYSVIEW_RecordEnterISR()     ← kırmızı bölüm burada başlar
        → RingBuffer_Alloc / ButtonQueue gönderimi
        → App_SystemView_RecordISRExit()     ← kırmızı bölüm burada biter
      → callback, HAL ve handler dönüşleri
```

app_button.c:38 giriş kaydı ve :56 çıkış kaydı callback içindedir. stm32f7xx_it.c:200’deki gerçek EXTI handler’ın sınırlarında kayıt yok. Dolayısıyla kırmızı çubuk, kabul edilen butonun işlenen kısmını ölçer; tüm donanım ISR süresini veya fiziksel kenardan kesmeye giriş gecikmesini ölçmez. Debounce/STOP nedeniyle erken dönen kesmeler görünmez. Çıkışın scheduler olarak etiketlenmesi, dönüş zincirinin bir kısmını Scheduler aralığında gösterebilir.

Üç seçili oturumda her görünen EXTI56 kaydı içinde bir ButtonQueue gönderimi var. Buna karşılık dosyaların eski oturumlarında kuyruk gönderimsiz IRQ’lar da bulunuyor. Bu gözlem, bütün dosyayı tek ve değişmez ölçüm yerleşimiyle yorumlamamak gerektiğini gösteriyor; eski firmware sürümü bu kayıtlardan kesin belirlenemez.

## 4. Ölçüm yöntemi ve güven sınırları

Task Ready → ilk Task Run farkı, ilk çalıştırılma beklemesidir. Task aktivasyonu, Task Run → Task Block aralığıdır; tablodaki aktivasyon süresinden görünen buton ISR aralıkları çıkarıldı. Kaydın başında zaten çalışan task’ın eksik ilk aktivasyonu ve telemetri üretmeden kapanan aktivasyonlar tam telemetri örneklerine dahil edilmedi. 2 ms kaydının ilk TelemetryTask aktivasyonu bu nedenle dışarıda bırakıldı.

Yüzdeler, ilk tam telemetri aktivasyonunun TxQueue gönderiminden son tam aktivasyonun gönderimine kadar ölçüldü. Böylece yüksüz kaydın yaklaşık 1,294 saniyelik telemetrisiz son bölümü karşılaştırmayı bozmaz. Kayıtta görünen task/Idle/ISR/Scheduler bağlamları zaman boyunca bölüştürüldü; toplamları %100. Task Block ile sonraki Task Run arasındaki geçiş giderinin bir kısmı önceki bağlama yazılabildiği için yüzde ve aktivasyon süresi birebir aynı tanım değildir.

Bu yüzdeler filtrelenmiş trace bağlam paylarıdır; kesin fiziksel CPU yükü değildir. SysTick, UART ve DMA kesmeleri kaydedilmediğinden süreleri task/Idle bağlamına dahil olabilir. SystemView paketleme ve taşıma maliyeti de ölçülen yürütmenin içindedir.

Kuyruk başarı ve başarısızlık hook’ları aynı event ID ve aynı parametre biçimini kullanıyor. Özellikle boş CmdQueue kontrolü de Function #92 üretir. Bu sebeple her #92 satırı alınmış bir mesaj sayılmaz; bu veriden kesin kuyruk doluluğu, UART hatasızlığı veya sıfır uygulama kaybı çıkarılmadı. Trace overflow görülmemesi, uygulama seviyesinde paket kaybı olmadığı anlamına gelmez.

## 5. Üç senaryonun karşılaştırması

| Ölçüm | Yüksüz | 2 ms yük | 5 ms yük |
| --- | --- | --- | --- |
| Son oturum süresi (s) | 10,940 | 13,521 | 12,079 |
| Karşılaştırma penceresi (s) | 9,640 | 13,510 | 12,070 |
| Tam telemetri aktivasyonu sayısı | 965 | 1352 | 1208 |
| Telemetry aktivasyonu medyan (ms) | 0,157 | 2,158 | 5,159 |
| Telemetry aktivasyonu P95 (ms) | 0,158 | 2,159 | 5,160 |
| Telemetry bağlam payı (%) | 1,69 | 21,69 | 51,70 |
| Idle bağlam payı (%) | 97,47 | 77,44 | 47,43 |
| UART task bağlam payı (%) | 0,82 | 0,84 | 0,84 |
| Telemetri kuyruk gönderim periyodu medyan (ms) | 10,0000 | 10,0000 | 10,0000 |
| Periyot min–maks (ms) | 9,972–10,027 | 9,936–10,065 | 9,937–10,064 |
| Telemetry Ready beklemesi medyan (µs) | 14,053 | 14,051 | 14,037 |
| Görünen buton ISR / işlenen BTN sayısı | 15 / 15 | 20 / 20 | 16 / 16 |
| ISR ölçülen kısmı medyan (µs) | 30,051 | 30,315 | 30,137 |
| Button Ready beklemesi medyan (µs) | 14,866 | 15,197 | 16,333 |
| Button Ready beklemesi P95 (µs) | 24,011 | 1320,880 | 3538,139 |
| Button Ready beklemesi en fazla (µs) | 29,870 | 1377,102 | 3726,125 |
| 1 ms üzerinde bekleyen ButtonTask | 0 / 15 | 3 / 20 | 7 / 16 |
| UART Ready beklemesi en fazla (µs) | 154,861 | 163,486 | 4833,111 |
| Trace overflow olayı | 0 | 0 | 0 |

TelemetryTask’ın medyan süresi yüksüz tabanın üzerine yaklaşık 2,001 ms ve 5,002 ms eklenerek artıyor. Bu, dosya etiketlerinin ölçümle uyumlu olduğunu destekliyor. Periyotlar yaklaşık 10 ms kalıyor; görülen en büyük tam-aktivasyon gönderim aralığı 10,065 ms. Bu kayıt pencerelerinde kaçırılmış 10 ms telemetri çevrimi işareti görülmedi; bu sonuç bütün olası yükler için garanti değildir.

ButtonTask medyan beklemesi üçünde de yaklaşık 15–16 µs. Buna rağmen yük altında kuyruk dağılımının uç kısmı büyüyor: 5 ms yükte 16 butonun 7’si, 2 ms yükte 20 butonun 3’ü 1 ms’den uzun bekliyor. Yalnız ortalama/medyan üzerinden değerlendirme yapmak bu gecikmeleri gizler. Buton örnek sayıları küçük olduğundan P95 gözlenen dağılımı özetler, gerçek zamanlı üst sınır değildir.

## 6. En uzun buton gecikmesinin olaylarla kanıtı

5 ms yük / oturum 6. Zamanlar o oturumun başlangıcına göredir. Olay numarası, en büyük dosyanın sıfır tabanlı decoder indeksidir; SystemView arayüzünün satır numarasıyla aynı olmak zorunda değildir.

| Olay | Zaman (s) | Kayıt | Açıklama |
| --- | --- | --- | --- |
| 103165 | 4,712787426 | Task Run | TelemetryTask |
| 103166 | 4,714267681 | ISR Enter | Button_EXTI15_10 |
| 103167 | 4,714283134 | Queue Send From ISR | ButtonQueue |
| 103168 | 4,714292144 | Task Ready | ButtonTask |
| 103169 | 4,714297787 | ISR Exit |  |
| 103170 | 4,717984995 | Queue Send | TxQueue |
| 103171 | 4,717997759 | Task Ready | UartTxTask |
| 103172 | 4,718008519 | Task Block | TelemetryTask / Blocked |
| 103173 | 4,718018269 | Task Run | ButtonTask |
| 103174 | 4,718026269 | Queue Receive | ButtonQueue |
| 103175 | 4,718119102 | Queue Send | TxQueue |
| 103176 | 4,718140907 | Task Block | ButtonTask / Blocked |

ButtonTask 4,714292144 s’de Ready oluyor; ISR 4,714297787 s’de normal dönüş kaydı üretiyor. TelemetryTask kaldığı yerden devam ediyor ve 4,718008519 s’de Blocked oluyor. ButtonTask ancak 4,718018269 s’de çalışmaya başlıyor. Ready beklemesi 3.726,125 µs: görünen ISR kalan kısmı 5,644 µs; Telemetry bağlamında geçen kısmı 3.720,481 µs. Son sayı, filtre nedeniyle gizli kesme/geçiş giderlerini de içerebilir.

2 ms yükte en uzun örnekte aynı desen var: 1.377,102 µs Ready beklemesinin 5,287 µs’si görünen ISR kalan kısmı; 1.371,815 µs’si Telemetry bağlamı. Yüksüzde en uzun bekleme 29,870 µs; görünen paylar 5,755 µs ISR ve 24,116 µs Scheduler. Buradaki temel etki, TelemetryTask’ın ButtonTask’tan yüksek öncelikli olmasıdır; buton ISR’ının milisaniyeler sürmesi değildir.

UART tarafında da 5 ms yükte 4.833,111 µs Ready beklemesi görülüyor ve bu aralık Telemetry bağlamında geçiyor. Bu ölçüm, UART hat hızının veya DMA aktarımının tek başına 4,833 ms sürdüğünü göstermez; UART task’ının işlemciye erişme gecikmesidir.

## 7. Kuyruk zinciri ve ölçülebilen yanıt süresi

| Ölçüm | Yüksüz | 2 ms yük | 5 ms yük |
| --- | --- | --- | --- |
| ButtonQueue ISR gönderimi / ButtonTask alımı / TxQueue BTN gönderimi | 15 / 15 / 15 | 20 / 20 / 20 | 16 / 16 / 16 |
| Telemetry → TxQueue gönderim olayı | 965 | 1353 | 1208 |
| ISR kayıt başlangıcı → ButtonQueue alımı, en fazla (µs) | 62,065 | 1409,005 | 3758,588 |
| ISR kayıt başlangıcı → BTN TxQueue gönderimi, en fazla (µs) | 164,042 | 1501,491 | 3851,421 |

Buton eşleştirmesi EXTI56 içindeki gönderimden, ButtonTask’ın ButtonQueue alımına ve ardından TxQueue gönderimine kadar yapıldı. Üç seçili oturumda eşleşmeden kalan buton gönderimi yok. Bu, görünen buton olaylarının task tarafından işlendiğine dair kanıttır; fiziksel olarak basılan toplam buton sayısı değildir.

Queue ID eşlemesi, kodun tek üretici/tüketici yollarıyla yapıldı: ButtonQueue 0x20003970; TxQueue 0x20003A48; CmdQueue 0x200080A0. Kuyruk adları bu kayıtlarda ayrı NameResource paketiyle gelmiyor. Function #90 = veri kuyruğuna gönderim; #92 = kuyruktan alma/başarısız alma; #96 = ISR’dan gönderim. Bunlar istenen kuyruk kayıtlarıdır.

NoLoad sonunda TelemetryTask üretimi duruyor; UartTxTask’ın 20 ms boş-kuyruk bekleme döngüsü sürüyor. Bu nedenle o dosyada TxQueue alma olayı sayısının gönderim sayısından büyük olması tek başına veri tutarsızlığı değildir: boş kuyruk timeout’u da #92 üretir.

20 ms uçtan uca deadline için kesin geçti/kaldı sonucu üretilmedi. Projede deadline R = t4 − t0. Bu trace, uygulamanın t0/t4 değerlerini ve USART1 son-bit tamamlanmasını açıkça taşımıyor; ISR işareti de t0’dan sonra. Buradaki 0,164 / 1,501 / 3,851 ms maksimum değerler yalnızca kaydedilmiş ISR başlangıcından BTN’nin TxQueue’ya bırakılmasına kadardır. UART iletim bitişi dahil değildir.

## 8. Kayıttan kesin çıkarılamayanlar

Kuyruk doluluğu tepe değeri, bütün UART/DMA hataları, fiziksel buton kenarından gecikme, gerçek toplam IRQ süresi, saf scheduler maliyeti ve uygulamanın uçtan uca t4−t0 sonucu bu filtreli kayıtlardan tek başına hesaplanamaz. Görseldeki bütün gri boşluklar “CPU boşta” veya “saf scheduler gecikmesi” olarak yorumlanmamalıdır.

defaultTask için başlangıç durumunda Cause=27 ve hiç Task Run olmaması, askıda kalmasıyla uyumlu. ButtonTask, TelemetryTask, UART ve Tmr Svc için Cause=4 bekleme/bloklanmayı temsil ediyor. Snapshot başlangıcındaki durum kayıtları bir kullanıcı suspend çağrısı sayılmadı.

Stack Info alanları bellek güvenliği değerlendirmesinde kullanılmadı: mevcut adapter, uxTaskGetStackHighWaterMark sonucunu StackSize alanına aktarıyor ve StackUsage sıfır kalıyor. Bu alanı doğrudan ayrılmış stack boyutu/ölçülmüş kullanım yüzdesi olarak yorumlamak doğru olmaz. Bu analiz firmware’de bir değişiklik yapmıyor.

## 9. UART task neden iki, üç, hatta dört kez çalışıyor?

Bu bölüm ikinci incelemede eklendi. Önceki sürüm, DMA beklemesini belirtmesine rağmen çalışma bloklarının sayısını açıklamıyordu. Üç seçili oturumun bütün tam telemetri gönderim aralıkları yeniden tarandı; yalnız iki kullanıcı örneğiyle yetinilmedi.

| Ardışık iki TEL kuyruk gönderimi arasında | Yüksüz | 2 ms yük | 5 ms yük |
| --- | --- | --- | --- |
| 2 UART Task Run olayı | 949 | 1331 | 1191 |
| 3 UART Task Run olayı | 7 | 6 | 10 |
| 4 UART Task Run olayı | 8 | 14 | 6 |

Pencereler iki ardışık tam telemetri aktivasyonunun TxQueue gönderim olayları arasındadır; fiziksel buton deneyi sayısı değildir. Pencere sınırından hemen önce basılan butonun işlenmesi sonraki pencereye taşınabilir. Bu nedenle aynı pencerede ISR görünmeden üç UART çalışması görülebilir. Seçili aralıklarda ikiden az veya dörtten fazla UART Task Run içeren tam pencere yok.

### 9.1. Tek TEL paketi varken neden iki çalışma bölümü var?

Birinci çalışmada UART task TxQueue’dan çerçeveyi alır, HAL_UART_Transmit_DMA ile aktarımı başlatır ve TxDoneSem üzerinde bloklanır. Son bitin UART’tan çıkmasının ardından HAL_UART_TxCpltCallback semaphore’u verir. İkinci çalışmada aynı fonksiyon beklediği satırdan devam eder; send_item tamamlanır, CmdQueue kontrol edilir ve boş TxQueue üzerinde tekrar beklenir. İki çalışma, iki gönderim veya task’ın yeniden oluşturulması değildir.

Dayanak: app_tasks.c:27–53, :179–210; app_hal_callbacks.c:18–31. Örnek A, olay 172742–172756.

| Örnek A / 2 ms kayıt | Olay indeksi | Oturum zamanı (ms) |
| --- | --- | --- |
| TEL → TxQueue | 172744 | 50,780940 |
| UART ilk Run | 172747 | 50,816023 |
| UART Blocked: gönderim bekleniyor | 172749 | 50,844861 |
| UART Ready | 172751 | 51,535222 |
| UART ikinci Run | 172752 | 51,550019 |
| UART Blocked: yeni TX işi bekleniyor | 172754 | 51,579218 |

[Örnek A — ham olaylar](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_A.csv>)

### 9.2. Neden yaklaşık 690 µs; bu task’ın periyodu mu?

Hayır. Çerçeve 64 bayt, USART1 921600 baud ve 8N1: 64 × 10 / 921600 = 694,444 µs nominal hat süresi. DMA task bloklanmadan önce başlatılır; bu yüzden Blocked → Ready ölçümü nominal hat süresinden biraz kısa olabilir. Donanım baud bölücüsü, interrupt gecikmesi ve kayıt noktaları da küçük fark oluşturur. Örnek A’da Blocked → Ready 690,361 µs; Ready → Running 14,796 µs; iki Run başlangıcı arasındaki süre 733,995 µs. Bunlar farklı ölçümlerdir.

Dayanak: app_config.h:59; main.c:244–247; Örnek A.

| 600–800 µs bandındaki UART Blocked → Ready aralıkları | Yüksüz | 2 ms yük | 5 ms yük |
| --- | --- | --- | --- |
| Örnek sayısı | 997 | 1372 | 1224 |
| Medyan (µs) | 690,194 | 690,204 | 690,181 |
| Min–maks (µs) | 685,454–717,773 | 689,593–692,227 | 688,958–690,833 |

Bu tablo süre bandı sayımıdır; tek başına başarıyla gönderilmiş frame sayacı değildir. UART/DMA hata callback’i de semaphore verebilir. Normal aktarım açıklaması, kaynak akışı, tekrarlanan süre ve kuyruk olayları birlikte değerlendirilerek yapılmıştır.

### 9.3. Buton TelemetryTask çalışırken gelince neden üç kez çalışıyor?

Buton ISR’ı ButtonQueue’ya yazar; ButtonTask hazır olur. Daha yüksek öncelikli TelemetryTask devam eder, TEL’i TxQueue’ya bırakıp bloklanır. ButtonTask UART’tan yüksek öncelikli olduğu için önce o çalışır ve BTN’yi aynı FIFO’ya ekler. UART başladığında TEL ve BTN birlikte bekler. Run 1 TEL aktarımını başlatır. Run 2 TEL’in tamamlanmasını işler ve BTN aktarımını başlatır. Run 3 BTN’nin tamamlanmasını işler ve yeni iş bekler. Bu örnekte iki paket, üç Run bölümü vardır.

Dayanak: app_config.h:106–108; app_tasks.c:247, :317; Örnek B.

| Örnek B / 2 ms kayıt | Olay | Zaman (ms) |
| --- | --- | --- |
| Buton ISR başlangıç kaydı | 176854 | 2960,607889 |
| TEL → TxQueue | 176858 | 2960,836764 |
| BTN → TxQueue | 176863 | 2960,976486 |
| UART Run 1 / TEL | 176865 | 2961,008306 |
| UART Run 2 / TEL bitişi + BTN başlangıcı | 176870 | 2961,738704 |
| UART Run 3 / BTN bitişi | 176876 | 2962,478477 |

Örnek B’de iki Blocked → Ready süresi 690,556 µs ve 690,333 µs’dir. Önceki sohbet yanıtında yuvarlanmış zamanlardan verilen son basamaklar burada ham cycle farkıyla hesaplandı.

[Örnek B — ham olaylar](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_B.csv>)

### 9.4. İki paket her zaman üç Run mı demektir? Dört Run neden görülüyor?

Hayır. TEL aktarımı tamamlanıp UART boş kuyrukta yeniden beklemeye geçtikten sonra buton gelirse, BTN için ayrı bir gönderim/tamamlama çifti oluşur: TEL için iki, BTN için iki; toplam dört Run. Run sayısı paket sayacı değildir. Genel olarak preemption veya başka işlerin araya girmesi de çalışma parçalarını artırabilir; bunlar ayrı olay zinciriyle incelenmelidir.

Dayanak: Örnek G: TEL Run’ları 1240,815745 ve 1241,549481 ms; buton ISR 1245,299625 ms; BTN Run’ları 1245,488991 ve 1246,217269 ms.

[Örnek G — dört UART çalışma bölümünün tamamı](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_G.csv>)

### 9.5. Buton daha önemliyse neden TEL önce gönderiliyor?

Task önceliği, FIFO’daki mesaj sırasını değiştirmez. Bu yapılandırmada ortak TxQueue vardır. TelemetryTask TEL’i önce koyduğu için UART önce TEL’i alır. ButtonTask’ın UART’tan yüksek öncelikli olması, BTN’yi kuyruğun önüne taşımaz. CMSIS osMessageQueuePut içindeki msg_prio parametresi bu FreeRTOS wrapper’ında ayrıca kullanılmıyor. Başlamış DMA aktarımı da buton geldi diye kesilmiyor.

Dayanak: app_config.h:37, :92–93; app_rtos.c:57–58; cmsis_os2.c:1651 ve xQueueSendToBack çağrıları; Örnek B.

### 9.6. DMA varsa task neden tekrar uyanıyor? İkinci çalışmada neyi tamamlıyor?

DMA baytları UART’a taşır; uygulamanın hata kontrolünü, sonraki frame seçimini, ring kaydını veya komut işleme döngüsünü yürütmez. Gönderimden sonra uart_tx_send semaphore’dan çıkar, hata bayrağını kontrol eder ve t4’ü alır. TEL için send_item döner; BTN için ring kaydına sonuç yazılır. Aynı statik TX buffer ancak bu tamamlanma yolundan sonra yeni frame ile doldurulur.

Dayanak: app_tasks.c:23–76; app_hal_callbacks.c:30–31.

### 9.7. DMA tamamlanması ile UART tamamlanması aynı an mı?

Normal DMA modunda son baytın UART veri register’ına aktarılması, son stop bitinin hattan çıkması değildir. HAL’in UART_DMATransmitCplt yolu UART TC kesmesini etkinleştirir; son-bit tamamlanması yolunda kullanıcı HAL_UART_TxCpltCallback çağrılır. Bu projede TxDoneSem o callback’ten verilir. SystemView’da bu IRQ’lar filtrelendiği için kırmızı bir DMA/UART bölümü görmeden task Ready olabilir.

Dayanak: stm32f7xx_hal_uart.c:UART_DMATransmitCplt ve UART_EndTransmit_IT; app_hal_callbacks.c:18–31.

## 10. Komutlar, kayıt sonu ve telemetri dışındaki UART çalışmaları

### 10.1. Telemetri durduktan sonra UART neden 20 ms’de bir çalışıyor?

UartTxTask boş TxQueue üzerinde osMessageQueueGet(...,20u) ile bekliyor. Veri gelmezse bekleme süresi dolar; task CmdQueue’yu kontrol eder ve yeniden TX kuyruğuna döner. Bu, uygulamanın komutları da aynı task içinde işlemesinden kaynaklanan zaman aşımı döngüsüdür. SystemView için eklenmiş polling değildir. 20 tick bekleme, tick fazına göre tam 20.000 µs olmak zorunda değildir; Ready olduktan sonra scheduler beklemesi de eklenebilir.

Dayanak: app_tasks.c:187–207; NoLoad son bölümünde 19–21 ms bandında 64 Blocked → Ready aralığı.

### 10.2. CmdQueue’ya komut geldiğinde UART neden hemen uyanmıyor?

Task o anda CmdQueue üzerinde değil, TxQueue veya TxDoneSem üzerinde bekliyor olabilir. Seçili ortak-FIFO modunda CmdRx yalnız CmdQueue’ya yazar; ayrıca UART task’ına notification göndermez. Bu yüzden başka nesneyi bekleyen task, komut sıraya girdi diye doğrudan uyanmaz. NoLoad kaydında iki CmdQueue gönderiminden sonraki ilk CmdQueue alma kayıtlarına kadar 9.680,204 µs ve 7.934,532 µs geçiyor. Komut payload’ı trace’de olmadığı için komut adını doğrudan okumuyoruz.

Dayanak: app_cmd_rx.c:handle_byte; APP_TX_MODE_DUAL_QUEUE kapalı; Örnek D ve E.

Kaynak yorumundaki “en geç 20 ms” ifadesi mutlak uçtan uca garanti olarak kullanılmamalıdır: 20 tick bir bloklanma timeout’udur; yüksek öncelikli task/ISR çalışmaları, devam eden UART gönderimi ve komut işleme süresi ayrıca etkiler.

[Örnek D — komut, gecikmeli işleme ve üretimin durması](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_D.csv>)

### 10.3. STOP sonrasında TelemetryTask neden bir kez daha kısa çalışıyor?

Task osDelayUntil içinde beklerken STOP g_running bayrağını temizleyebilir. Bekleme süresi dolduğunda task uyanır, if (!g_running) kontrolünde üretim döngüsünden çıkar ve RunFlags beklemesine geçer. NoLoad örneğinde son TEL 9645,731005 ms’de kuyruğa giriyor; 9655,601889 ms’de TelemetryTask bir daha çalışıyor, 10,801 µs sonra yeni TEL göndermeden bloklanıyor. Bu desen STOP yolu ile uyumludur; kısa çalışmayı yeni bir telemetri çevrimi olarak saymadık.

Dayanak: app_tasks.c:129–136, :267, :287–290; Örnek D olay 172193–172196.

### 10.4. TX kuyruğu alımı olmadan art arda UART Run/Block neden var?

Bu projede DUMP ve BOOT çerçeveleri TxQueue üzerinden geçmez; send_meta doğrudan uart_tx_send çağırır. NoLoad kaydında ikinci komutun işlenmesini takiben 17 adet yaklaşık 690 µs bekleme var; arada yeni TxQueue alma olayı yok. 15 BTN kaydı + STAT + DUMP_END = 17 çerçeveli dump yolu ile güçlü biçimde uyumlu. Komut metni ve UART payload’ları kayıtta bulunmadığından bunu kesin çözülmüş DUMP komutu olarak değil, kaynak ve örüntüye dayanan çıkarım olarak işaretliyoruz.

Dayanak: app_tasks.c:80–104 ve CMD_DUMP; Örnek E, 172315–172384.

Bu bölümde UART çalışma parçaları yaklaşık 0,10–0,19 ms’ye uzayabilir; dump kayıtlarını çerçeveye dönüştürme ve sonraki aktarımı hazırlama işi vardır. İkinci inceleme, ilk rapordaki telemetrisiz son bölüm açıklamasını bu 17 aktarım benzeri beklemeyle tamamlıyor.

[Örnek E — 17 beklemeli gönderim dizisi](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_E.csv>)

### 10.5. 2 ms kaydında 1373 TX alma olayı ama 1372 kısa tamamlanma beklemesi var. Bir paket kayıp mı?

Bu fark tek başına kayıp kanıtı değildir. Son TxQueue alma olayı 13520,823977 ms; UART Blocked kaydı 13520,844343 ms; Trace Stop 13521,382181 ms. Kayıt, bloklanmanın yalnız 537,838 µs sonrasında bitiyor. Tipik tamamlanma beklemesi yaklaşık 690 µs olduğundan son aktarımın tamamlanması kaydın dışında kalabilir. Record Stop, USART1 uygulama gönderimini durdurma komutu değildir.

Dayanak: Örnek F; SEGGER_SYSVIEW_Stop_Ex izleme akışını durdurur, uygulamanın CMD_STOP yolundan ayrıdır.

[Örnek F — kayıt sınırında tamamlanması gözlenemeyen aktarım](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_F.csv>)

## 11. Task, ISR, kuyruk ve zaman çizelgesi için soru-cevap rehberi

### 11.1. Task Run, ButtonTask_Run fonksiyonunun baştan çağrılması mı?

Hayır. Task bir kez oluşturulur, sonsuz döngüde yaşar. Run kaydı scheduler’ın o task’ı işlemci üzerinde yürütmek üzere seçmesini bildirir; task beklediği veya kesildiği yerden devam eder.

Dayanak: app_rtos.c:90; app_tasks.c:216–257; tasks.c:3006–3007; traceTASK_SWITCHED_IN.

### 11.2. Gri çubuk ile task satırındaki açık arka plan aynı şey mi?

Hayır. Ready olayıyla başlayan gri aralık hazır fakat henüz çalıştırılmayan task’ı gösterir. Satır boyunca uzanan soluk yaşam arka planı veya kayıtsız bir boşluk tek başına Ready kanıtı değildir. Seçili olayın zamanını, Task Ready hedefini ve sonraki Task Run’ı birlikte kontrol et.

Dayanak: Ready/Run eşleştirmeleri: *_waits.csv; SystemView Timeline gösterimi.

### 11.3. Ready olduktan sonra neden hemen Running olmuyor?

Ready kaydı kernel hazır-listesi güncellenirken üretilir; ilgili kritik bölüm/ISR henüz bitmemiş olabilir. Task değişimi PendSV üzerinden tamamlanır. Daha yüksek öncelikli task çalışıyorsa önce onun bloklanması gerekir. Trace çağrıları da zaman tüketir. Ready→Run farkını tek başına interrupt gecikmesi veya scheduler maliyeti diye adlandırma.

Dayanak: tasks.c:219, :2759; port.c:xPortSysTickHandler; örnek C.

### 11.4. Task Ready satırının Context alanında Idle veya başka task yazması hata mı?

Context, olayın kaydedildiği son görünen bağlamdır; Ready olan task Event/Detail hedefindeki task’tır. Örneğin TelemetryTask’ın kuyruğa yazması UART’ı hazır yapabilir. SysTick/UART IRQ kayıtları kapalı olduğunda bir ISR içindeki Ready olayı bile Idle veya task bağlamında görünebilir.

Dayanak: traceMOVED_TASK_TO_READY_STATE; filtreli IRQ hook’ları; örnek A, 172745.

### 11.5. Buton ISR’ı neden bazen TelemetryTask’a geri dönüyor?

ButtonTask’ın önceliği 24, TelemetryTask’ın 32. ISR, butonu hazır yapar ama bu durum çalışan daha yüksek öncelikli TelemetryTask’ı kesip ButtonTask’a geçmeyi gerektirmez. TelemetryTask devam edip bloklandığında ButtonTask çalışır.

Dayanak: xTaskRemoveFromEventList içindeki uxPriority karşılaştırması; örnek B/C.

### 11.6. ISR Exit ile ISR Exit To Scheduler farkı ne?

app_systemview.c, çıkış işareti üretirken SCB ICSR içindeki PendSV pending bitine bakar. Bit set ise ToScheduler; değilse normal ISR Exit kaydı üretir. Bu fonksiyon kendi başına task çalıştırmaz. Pending bit başka bir olay nedeniyle de set olmuş olabilir; her ToScheduler satırı kesinlikle butonun tetiklediği ayrı context switch sayılmaz.

Dayanak: app_systemview.c:159–168; CMSIS osMessageQueuePut → portYIELD_FROM_ISR.

### 11.7. Butonun kesme önceliği 5, TelemetryTask’ın 32. Hangisi daha yüksek?

Bunlar farklı öncelik uzaylarıdır. NVIC’de küçük sayısal değer daha yüksek kesme önceliğidir; FreeRTOS’ta büyük task önceliği daha yüksektir. NVIC 5 ile RTOS 32 doğrudan karşılaştırılmaz. Bir donanım kesmesi, maskelenmediği ve işlemcinin mevcut exception önceliği izin verdiği anda task yürütmesini kesebilir.

Dayanak: main.c:348; FreeRTOSConfig.h:128–141; app_config.h:106–108.

### 11.8. Kırmızı çubuk başlamadan önceki 10–15 µs boşluk kesin ne?

Bu kayıtla tek bir nedene kesin atanamaz. Ready işaretinden sonra devam eden kernel/trace işlemleri, görünmeyen IRQ’lar, EXTI’nin kaydedilmeyen HAL/debounce kısmı ve geçiş gideri olası paylardır. Mevcut ISR başlangıç çağrısı handler başında değildir. Gerçek ISR giriş anını bilmiyoruz.

Dayanak: app_button.c:24–38; stm32f7xx_it.c:200–210; bölüm 3.

### 11.9. Buton ISR süresi neden bazen yaklaşık 18, bazen 30 µs?

İşaretlerin arasındaki yol ve koşullar değişebilir: kuyrukta bekleyen task’ı uyandırma, RingBuffer durumları ve trace taşımasının meşgul olup olmaması gibi. Bu kayıt bu mikro farkları fonksiyon bazında ayırmıyor. Tek başına bu iki kümeye belirli bir sebep atamak kanıtlanmış olmaz.

Dayanak: app_button.c:38–56; app_ring_buffer.c:RingBuffer_Alloc; app_systemview.c:try_send.

### 11.10. Cause=4, 4 ms beklemek anlamına mı geliyor?

Hayır; süre değil durum kodudur. Bu entegrasyonda Blocked için 4 kullanılıyor. Queue, semaphore, event flag veya zaman bekleme aynı kodla gelebilir. Nedenini çevredeki kuyruk olayları ve kaynak akışıyla ayırmak gerekir.

Dayanak: SEGGER_SYSVIEW_FreeRTOS.h:302–308.

### 11.11. osWaitForever neden Suspended değil?

Bir queue/event üzerinde süresiz bekleyen task olay geldiğinde otomatik Ready olur; mantıksal durumu Blocked’dır. Kernel süresiz beklemeyi xSuspendedTaskList içinde tutabilse de bu açık vTaskSuspend çağrısıyla aynı anlamı taşımaz. Entegrasyon bu ayrımı ayrı hook’larla koruyor.

Dayanak: tasks.c:5117–5124; traceMOVED_TASK_TO_BLOCKED_LIST ve traceMOVED_TASK_TO_SUSPENDED_LIST.

### 11.12. defaultTask ve Tmr Svc neden görünür ama çalışmıyor?

Task Info listesinde bulunmaları CPU kullandıklarını göstermez. defaultTask kendini suspend eder. Tmr Svc timer servis görevidir; incelenen oturumlarda Blocked snapshot’ı var ve çalışma olayı yok. Onları grafikte silmek ile runtime maliyetini azaltmak aynı işlem değildir.

Dayanak: main.c:366–374; FreeRTOSConfig.h:configUSE_TIMERS; başlangıç Task Info/Block paketleri.

### 11.13. Idle çalışıyorsa UART aktarımı durmuş mudur?

Hayır. CPU’da çalışmaya hazır uygulama task’ı kalmadığında Idle seçilebilir; UART/DMA donanımı aktarımı paralel sürdürür. Idle yüzdesi ayrıca düşük güç moduna girildiğinin kanıtı değildir; WFI/sleep davranışı ayrı incelenir.

Dayanak: Örnek A 172749–172752; app_tasks.c:41–45.

### 11.14. Semaphore kayıtları kapalıyken UART Ready olayını nasıl görüyoruz?

Semaphore API çağrısının kayıt filtresi ile task durum hook’ları ayrıdır. TxDoneSem’in verilmesi FreeRTOS hazır listesini değiştirir; Task Ready hook’u hâlâ aktif olduğu için uyanış görülür.

Dayanak: traceQUEUE_SEMAPHORE_RECEIVE no-op; traceMOVED_TASK_TO_READY_STATE aktif; app_hal_callbacks.c:31.

### 11.15. Function #90/#92/#96 neden var; başka fonksiyonları kapatmamış mıydık?

Bunlar bırakılması istenen veri kuyruğu olaylarıdır. Bu kaynak sürümünde #90 gönderim, #92 alma girişiminin sonucu, #96 ISR’dan gönderim olayına karşılık gelir. İsimlerin arayüzde Function #... olması kayıt verisinin olmadığı anlamına gelmez; eşleme mevcut adapter makrolarından yapıldı.

Dayanak: SEGGER_SYSVIEW_FreeRTOS.h:apiID_OFFSET=32 ve queue trace makroları.

### 11.16. #92 her göründüğünde gerçekten paket alınmış mı oluyor?

Hayır. Başarılı ve başarısız alma hook’ları aynı ID/parametre düzenini kullanır. CmdQueue’nun boş olup olmadığını 0 timeout ile kontrol etmek de #92 üretir. Bir satırın başarısını ancak çevredeki akışın desteklediği ölçüde yorumlayabiliriz.

Dayanak: traceQUEUE_RECEIVE / traceQUEUE_RECEIVE_FAILED; queue.c ve app_tasks.c:189.

### 11.17. Queue Receive parametresindeki 20, 0 veya 4294967295 ne?

Bunlar bekleme tick sayılarıdır: 20 sınırlı bekleme, 0 beklemeden deneme, 4294967295 portMAX_DELAY/osWaitForever karşılığı. Başarısız alma hook’undaki 0, timeout işlenirken kalan bekleme değerinin sıfıra inmesinden de gelebilir. Bunları paket uzunluğu veya mesaj sayısı sanma.

Dayanak: queue.c:xQueueReceive ve xTaskCheckForTimeOut; traceQUEUE_RECEIVE makrosu.

### 11.18. Kuyruk doluluğu ve mesaj içeriği hangi alanlarda?

Mevcut olaylar güvenilir occupancy veya frame payload taşımaz. Gönderimde görülen pvItemToQueue bir adres olabilir; paketin kendisi değildir. ISR makrosundaki pxHigherPriorityTaskWoken da pointer adresidir; true/false değeri olarak okunmaz. Queue Receive makrosu buffer alanına ShrinkId(0) koyuyor; görünen 0xE0000000 bu yüzden gerçek hedef buffer adresi değildir.

Dayanak: SEGGER_SYSVIEW_FreeRTOS.h:219–232; Init RAMBase=0x20000000, shift=0.

### 11.19. Queue adı yokken ButtonQueue/TxQueue ayrımı neye dayanıyor?

Kayıttaki kaynak ID’leri ile kaynak kodun üretici/tüketici yolları eşleştirildi. EXTI56 içinde yazılan ve ButtonTask’ın aldığı kaynak ButtonQueue; Telemetry/Button tarafından yazılıp UART tarafından alınan TxQueue; UART RX’den yazılan CmdQueue. Bu kayıtlarda eşleme tutarlı; farklı firmware/heap yerleşiminde adreslerin aynı kalacağı varsayılmamalı.

Dayanak: Bölüm 7 adres eşlemesi ve *_queues.csv.

### 11.20. Yük 2 veya 5 ms iken niye task 2,158 veya 5,159 ms çalışıyor?

Yük döngüsüne ek olarak ölçülen task aralığında sayaç okuma, TEL çerçevesi üretme, queue işlemleri, trace gideri ve filtrelenen interrupt süreleri bulunur. Yüksüz taban yaklaşık 0,157 ms. Dosya etiketi bütün task’ın süresi değildir.

Dayanak: app_tasks.c:292–320; tam aktivasyon istatistikleri.

### 11.21. Gönderimler yaklaşık 10 ms aralıklıysa yeşil bölüm neden daha kısa?

10 ms üretim periyodu iki çevrim arasındadır. Yeşil bölüm task’ın bu çevrimde çalıştığı parçadır. osDelayUntil önceki wake hedefini ilerlettiği için normal durumda yük süresinin üstüne yeniden tam 10 ms ekleyerek uyumaz.

Dayanak: app_tasks.c:273–287; *_activations.csv ve complete_telemetry_send_interval_us.

### 11.22. Yük periyodu aşarsa ne olur; bu kayıtta olmuş mu?

Kod now >= wake olduğunda hedefi now + period yaparak eski periyotları art arda telafi etmeyi önler. Seçili tam çevrimlerde 9–11 ms bandı dışına çıkan gönderim aralığı gözlenmedi; bu pencerelerde aşırı yük kaynaklı kaçırılmış çevrim işareti yok. Her olası load_iter için zaman garantisi verilmez.

Dayanak: app_tasks.c:280–287; ölçülen min/maks periyotlar.

### 11.23. Yük arttığında neden bütün buton basışları gecikmiyor?

Buton, TelemetryTask bloklu veya CPU Idle iken gelirse hemen işlenebilir. Yük döngüsü sırasında gelirse yüksek öncelikli task’ın kalan süresini bekler. Gecikme basışın periyottaki fazına bağlıdır. 5 ms yükte medyanın küçük ama maksimumun büyük olması bu iki durumun birlikte bulunmasıyla uyumludur.

Dayanak: 5 ms: 7 görünen ISR Telemetry bağlamında, 8 Idle, 1 son görünen UART bağlamında; örnek C.

### 11.24. Bir task Run kaydı olmadan ISR’dan sonra çalışmaya devam edebilir mi?

Evet. Normal ISR Exit, kesilen bağlama dönüşü ifade eder. Task değişimi yoksa yeni Task Run hook’u gerekmez. Bu yüzden sadece Task Run satırı sayarak ISR’dan dönüşleri veya mantıksal iş adetlerini saymak doğru değildir.

Dayanak: SEGGER_SYSVIEW_RecordExitISR; örnek B’de ISR Exit sonrasında Telemetry queue gönderimi var.

### 11.25. Bu durum priority inversion mı, deadlock mu?

Bu örneklerde beklemeyi açıklayan görünen desen, daha yüksek öncelikli TelemetryTask’ın yürütülmesidir. Mutex tutan düşük öncelikli task nedeniyle bekleyen yüksek öncelikli task zinciri gösterilmiyor; bu örneğe priority inversion demek doğru olmaz. İlerleyen Task Run/queue olayları da incelenen zincirlerde deadlock olmadığını gösterir; tüm çalışma ömrünün garantisi değildir.

Dayanak: Örnek B/C ve task öncelikleri; ölçüm kapsamı seçili üç oturum.

### 11.26. SystemView kaydı sistemi etkiliyor mu; DMA bunu sıfırlar mı?

DMA sadece aktarımın bir kısmını donanıma devreder. Event paketleme, RTT buffer kopyalama, kritik bölümler ve UART/DMA callback’leri CPU zamanı alır. try_send ve RTT kilidi interrupt maskesini kullanır. Bu veriler izleme açıkken ölçüldü; aynı koşulda izleme kapalı karşılaştırma olmadığı için overhead yüzdesi hesaplanmadı.

Dayanak: app_systemview.c:try_send ve SEGGER_SYSVIEW_X_OnEventRecorded; SEGGER_RTT_Conf.h kilitleri.

### 11.27. 921600 baud bu kayıt için yetersiz mi?

Seçili oturumlarda overflow paketi yok ve decoder bütün paketleri çözüyor; görünen veride trace buffer taşması işareti bulunmadı. USART1 için yalnız 100 TEL/s, 64 bayt ve 8N1 nominalde 64.000 bit/s (~%6,94 hat payı) eder; BTN/dump ayrıca eklenir. USART6 ayrı trace hattıdır. Ortalama bant hesabı bütün burst ve bağlantı hatalarını garanti etmez.

Dayanak: main.c USART1/6 ayarları, APP_FRAME_SIZE, kayıt overflow sayacı. Bağımsız donanım yük/hat testi yapılmadı.

### 11.28. Ölçülen maksimum gecikme sistemin gerçek en kötü süresi mi?

Hayır. Üç kayıtta 15/20/16 buton örneği var. Maksimum yalnız gözlenen örneklerin maksimumudur; bütün fazlar, IRQ çakışmaları, kuyruk birikimi ve hata yolları taranmadı. P95 de bu küçük örneklemin betimsel yüzdeliğidir, kesin üst sınır değildir.

Dayanak: Örnek sayıları ve *_waits.csv; stat() lineer enterpolasyon kullanır.

### 11.29. Neden ready beklemesi, ISR→queue alımı ve R=t4−t0 farklı?

Ready beklemesi yalnız Ready→Run’dır. ISR işareti→ButtonQueue alımı ayrıca ISR’ın işaretlenmiş ilk kısmını ve queue alma yolunu içerir. Uygulama R=t4−t0 ise TX kuyruğunda bekleme ve UART son bitine kadar ilerler; mevcut trace bu son iki timestamp’i açıkça taşımıyor. Aynı isim altında karşılaştırılmamalıdır.

Dayanak: app_button.c:t_entry; app_tasks.c:t1/t2/t3; app_hal_callbacks.c:t4; bölüm 7.

### 11.30. Task Stop Ready veya Task Block, task silindi demek mi?

Hayır. Çalışabilir kümeden çıkıp beklemeye geçtiğini bildirir. Task silinmesi/sonlandırılması ayrı yaşam döngüsü olayıdır. Bu üç oturumda create/delete döngüsü görülmüyor; başlangıçta mevcut task’ların bilgileri ve durumları gönderiliyor.

Dayanak: Event 7 ile Task Terminate ayrı; traceTASK_DELETE ve SYSVIEW_SendTaskStates.

### 11.31. Kayıt başlangıcındaki Task Block/Ready gerçek bir yeni geçiş mi?

Başlangıç callback’i, kayıttan önce var olan durumları snapshot olarak gönderir. Bunlar bütün task’ların aynı anda yeni bloklandığı anlamına gelmez. İlk zaten-running aktivasyonun süresi soldan eksiktir; rapor bunu tam aktivasyon dağılımından çıkardı.

Dayanak: SYSVIEW_SendTaskStates; 2 ms kaydının ilk telemetri aktivasyonu.

### 11.32. Mutlak saat, kernel zamanı ve rapordaki zaman aynı mı?

Dosya başlığındaki RecordTime PC’deki kaydetme zamanı; Systime paketleri hedefin kernel süresi; olay farkları DWT cycle zamanlamasıdır. Rapordaki zaman her Start oturumunda sıfırlanır. Farklı oturumların son/başlangıcını birleştirerek uygulamanın kesintisiz gecikmesi hesaplanmaz. 216 MHz’de 32-bit sayaç yaklaşık 19,884 s’de döner; paket delta hesabı wrap’i işler, ham sayaç düşüşü otomatik geri-zaman hatası değildir.

Dayanak: SEGGER_SYSVIEW.c paket delta hesabı; _cbGetTime; decoder oturum sınırları.

### 11.33. Rapor mevcut kaynak ile karta yüklü firmware’in birebir aynı olduğunu kanıtlıyor mu?

Hayır. Kaynak hash’leri ve yerel ELF incelendi; task öncelikleri, saat ve olay desenleri uyumlu. Cihazdan firmware hash okunmadı ve kayıt derleme kimliği taşımıyor. Yerel kaynak/ELF ile ölçümün uyumlu olması, cihaz binary kimliğinin kriptografik doğrulaması değildir.

Dayanak: source_manifest.json; verification.json: hardware_firmware_identity_verified=false.

## 12. İnceleyenin kullanabileceği kontrol sırası ve açık sorular

```text
1. Dosya ve oturumu seç; eski birikimli oturumları ayır.
2. Ready/Run/Block hedef task’ını, Context alanından ayrı oku.
3. Queue ID ve kaynak kod yolunu eşleştir.
4. IRQ Enter/Exit sınırlarını ve filtreleri kontrol et.
5. Run bölümlerini tek iş değil, yürütme parçaları olarak say.
6. Başlangıç snapshot’ı ve son yarım işlemi ayır.
7. Olay farkını doğru adlandır: Ready beklemesi / UART beklemesi / task süresi.
8. Bilinmeyen payload, gerçek IRQ girişi veya t4 için kesin sonuç üretme.
```

| Henüz cevaplanamayan soru | Neden | Kesin cevap için gereken kanıt |
| --- | --- | --- |
| Gerçek ISR giriş/çıkış maliyeti kaç? | Kayıt callback ortasında; IRQ’lar filtreli | Handler sınırlarında ölçüm veya uygun donanım zaman ölçümü |
| Her buton için R=t4−t0 ≤20 ms mi? | Trace t0/t4/event_id zincirini taşımıyor | Aynı deneyin event_id içeren REC/t0–t4 kayıtları |
| FIFO anlık tepe doluluğu ve tüm drops kaç? | Başarı/başarısızlık aynı trace biçiminde | Kuyruk doluluk ve sonuç sayaçları / STAT kaydı |
| SystemView overhead yüzdesi kaç? | İzlemesiz eşlenik ölçüm yok | Aynı yük/derleme için izleme açık-kapalı karşılaştırma |
| Gerçek en kötü gecikme sınırı ne? | Örnek sayısı ve koşullar sınırlı | Faz taraması, IRQ/queue yük senaryoları ve zaman analizi |
| NoLoad’daki komutlar kesin STOP ve DUMP mı? | Komut metni kaydedilmiyor | Aynı oturumun komut/payload veya GUI log’u |
| Fiziksel buton basışlarının tümü kabul edildi mi? | Debounce ve g_running filtreleri kayıt dışında | Fiziksel kenar ve kabul/red sayaçları |

Bunlar bu çalışma için uygulanmış yeni firmware değişiklikleri değil; verinin cevap sınırlarıdır. Rapor, kanıtlanamayan ayrıntıları tahminle doldurmak yerine gereken ek kanıtı gösterir.

İkinci inceleme kapsamı: kaynak/girdi hash kontrolü; 191.946 benzersiz olayın çözümlemesi; üç son oturumun 3.522 tam telemetri gönderim aralığı; 51 buton zinciri; UART çalışma sayıları; kısa/uzun bloklanmalar; ISR dönüş türleri; komut sonrası davranış; kayıt sınırları; kuyruk sonuçlarının ayırt edilebilirliği; zaman/stack metadata sınırları. “Her olası sorunun cevabı bu dosyada var” garantisi verilmez; incelenen örüntüler, kanıtları ve bilinmeyenler açıkça listelenir.

[Olay örneği A — 2 ms: tek telemetri, iki UART çalışma bölümü](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_A.csv>)

[Olay örneği B — 2 ms: telemetri sırasında buton, üç UART çalışma bölümü](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_B.csv>)

[Olay örneği C — 5 ms: en uzun ButtonTask Ready beklemesi](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_C.csv>)

[Olay örneği D — Yüksüz: komut gelir ama UART hemen uyanmaz; üretim durur](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_D.csv>)

[Olay örneği E — Yüksüz: komut sonrası DUMP ile uyumlu gönderim dizisi](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_E.csv>)

[Olay örneği F — 2 ms: kayıt aktarım beklenirken biter](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_F.csv>)

[Olay örneği G — 2 ms: aynı periyotta dört UART çalışma bölümü](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/case_G.csv>)

[İkinci incelemenin sayısal sonuçları](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/audit_patterns.json>)

## 13. Doğrulama ve yeniden üretim

Üç dosyanın bütün baytları çözüldü. 478.272 paket çözümlemesi (tekrarlar dahil) yeniden kodlanarak orijinal paket baytlarıyla eşleştirildi. Birikimli dosya ilişkisi tam bayt önekiyle doğrulandı. Seçili oturumlarda 0 overflow, 0 açık ISR çifti, 0 tekrar Ready anomalisi ve 0 eşleşmemiş buton gönderimi var. Bağlam sürelerinin toplamı analiz penceresiyle eşit. Bu kontroller kayıt biçimi ve analiz tutarlılığını doğrular; cihazda izlenmeyen olayları görünür kılmaz.

```text
python Segger_output/Rapor_Yeni/analyze.py
python Segger_output/Rapor_Yeni/audit_patterns.py
python Segger_output/Rapor_Yeni/build_report.py
python Segger_output/Rapor_Yeni/verify_report.py
```

| Dosya / klasör | İçerik |
| --- | --- |
| rapor.md / rapor.html | Aynı analizin metin ve tarayıcı sürümü |
| metrics.json / sessions.csv | 10 oturumun sayısal sonuçları ve ana karşılaştırma |
| NoLoad_*, 2msLoad_*, 5msLoad_*.csv | Olaylar, task aktivasyonları, Ready gecikmeleri, ISR çiftleri, kuyruk işlemleri, buton eşleştirmeleri |
| decoded_summary.json | Girdi dosyalarının SHA-256 değerleri, boyutları ve olay sayıları |
| source_snapshot/ / source_manifest.json | İncelenen kaynakların kopyaları, hash değerleri ve Debug ELF hash’i |
| verification.json / decoder_checks.log | Paket/oturum/bağlam kontrol sonuçları |
| audit_patterns.json / *_uart_waits.csv / *_telemetry_windows.csv | UART tekrarları, komut beklemeleri ve tam periyot taraması |
| case_A.csv … case_G.csv / casebook.json | Yedi davranışın zaman ve olay numarasıyla ham kanıtları |
| isr-disassembly.txt | Mevcut Debug ELF’de buton ISR kayıt yerleşimi |

[Sayısal sonuçlar](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/metrics.json>)

[Oturum listesi](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/sessions.csv>)

[Doğrulama sonuçları](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/verification.json>)

[Kaynak dosya hash’leri](<C:/Users/Asus/Documents/GitHub/RTOS_Bootcamp/Hafta_1/Segger_output/Rapor_Yeni/source_manifest.json>)
