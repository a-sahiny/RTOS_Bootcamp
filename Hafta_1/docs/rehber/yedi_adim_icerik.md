# 0 | Hazırlık: isimler, bellek ve çalışmaya başlama

Bu belgede tek bir buton olayını takip ediyoruz. Her adımda dört soruyu cevaplayacağız: **Kim çalışıyor? Hangi veriyi alıyor? Hangi satır neyi değiştiriyor? İş bir sonraki yere nasıl geçiyor?** Kod kutuları mevcut kaynak dosyalardan alınmıştır; kaynak yolu ve satır numarası kutunun üstündedir. İsimleri ve parametreleri kodun hemen altında açıklıyoruz.

Ana yol, bu klasörde varsayılan olan APP_TX_MODE_LARGE_FIFO dalıdır: ButtonQueue 8, ortak TxQueue 256, CmdQueue 4 öğe. UART kaynakta 921600 baud; kernel tick 1 ms. Diğer derleme dalına ait #ifdef satırlarını gördüğünüzde bunlar aynı anda çalışan ikinci bir yol değildir. Burada izlediğimiz ortak FIFO yolu #else dalındadır. Bu belge, kart üzerinde yeni bir ölçüm yapıldığını iddia etmez.

## Önce birbirine benzeyen dört şeyi ayıralım

| İsim | Ne tür bir şey? | Nerede yaşar, ne taşır? |
| --- | --- | --- |
| ButtonEvent_t | Bir C veri tipi | Bir basışın zamanı, kimliği ve RAM slotunun indeksini bir araya getirir. |
| ButtonQueueHandle | RTOS kuyruk tanıtıcısı | Kuyruğun kendisi değil, oluşturulmuş ButtonQueue nesnesine erişim kimliğidir. Kuyruk ButtonEvent_t kopyaları saklar. |
| EventRecord_t ve s_records | Kayıt tipi ve bu tipten dizi | app_ring_buffer.c içinde 256 olayın ölçüm geçmişini RAM'de tutar. |
| TxItem_t ve frame[64] | Gönderim işi ve onun içindeki bayt dizisi | Kimlik bağlantısı ile hazır 64 baytlık mesajı birlikte taşır. |

RAM kaydı, ButtonQueue ve TxQueue birbirinin başka adı değildir. Kayıt geçmişi tutar; kuyruklar yapılacak işi taşır. Bir olay kuyruktan alınıp gönderilse de RAM kaydı deney sonundaki DUMP için kalır.

Kodda uint8_t, uint16_t ve uint32_t sırasıyla 8, 16 ve 32 bit işaretsiz sayı türleridir. typedef struct, ilgili alanları bir araya getirip yeni bir tipe isim verir. `sizeof(Tip)` bu tipin bellekte kaç bayt kapladığını derleyiciden alır. `0u`, işaretsiz sıfır sabitidir. `NULL`, geçerli bir nesne adresi verilmediğini belirtir. Tip adı nesnenin kendisi değildir: ButtonEvent_t bir kalıp; `ButtonEvent_t evt;` ise o kalıptan gerçek bir nesnedir.

## g_running tam olarak nedir?

:::source Src/app_rtos.c 18 28

`volatile bool g_running = false;` bir fonksiyon değil, bir değişken tanımıdır. bool yalnızca doğru/yanlış bilgisini ifade eder. false başlangıçta deneyin kapalı olduğunu söyler. `g_` ön eki global anlamını hatırlatan bir adlandırmadır; C dilinin özel komutu değildir.

Bu tanım app_rtos.c'de bellekte tek bir nesne oluşturur. app_rtos.h içindeki `extern volatile bool g_running;`, diğer dosyalara "aynı değişken başka yerde tanımlı" der; her dosya için ayrı g_running oluşturmaz. volatile, değeri ISR gibi farklı bir yürütme akışı görebileceği için derleyicinin gerekli okuma/yazmaları atlamamasını sağlar. **volatile bir mutex değildir, görevi uyandırmaz ve START komutunu kendi başına çalıştırmaz.**

g_running'i START ve STOP işleyicisi değiştirir. Button_HandleEXTI onu okuyup yeni basışı kabul edip etmeyeceğine karar verir. TelemetryTask_Run onu döngü koşulunda ve uyandıktan sonra kontrol eder. ButtonTask'ın olay işleme döngüsünde bu kontrol yoktur: daha önce kuyruğa kabul edilmiş olayı STOP'tan sonra da işleyebilir.

| Tanım | Anlamı | Yazan / kullanan |
| --- | --- | --- |
| g_run_start_ms | Son START anındaki milisaniye sayacı | START yazar; buton ve TEL, deney içi kronoloji üretir. |
| g_cfg.period_ms | TEL üretiminin hedef periyodu | Çalışma kapalıyken CFG yazar; TelemetryTask okur. |
| g_cfg.load_iter | Yapay hesap döngüsünün kaç tur döneceği | CFG yazar; TelemetryTask kullanır. Milisaniye değildir. |
| g_cfg.scenario_id | Deney etiketi | CFG yazar; olay ve mesajlara kopyalanır. Tek başına bir yük/periyot seçmez. |
| g_dwt_ok | Açılışta hassas sayacın ilerlediği görüldü mü? | Timestamp_Init sonucu; BOOT'ta raporlanır. |
| g_tx_done_cycles | En son TX tamamlanma callback'inin cycle değeri | Callback yazar; uart_tx_send t4 olarak okur. |
| g_tx_dma_error | Mevcut gönderimde DMA hatası bildirildi mi? | Gönderim başında false; hata callback'inde true. |
| RUN_BIT | RunFlags içindeki 0x00000001 bitinin adı | START set eder, STOP temizler; TelemetryTask bekler. |
| RING_INDEX_NONE | 0xFFFF, yani 65535 özel işareti | TEL öğesinde "bu mesajın bir buton kayıt slotu yok" anlamındadır. |
| REC_TS_T1 ... REC_TS_T4 | Ölçülmüş damgaları işaretleyen bitler | Değerin sıfır olmasını "ölçülmedi" diye yorumlamamak için vardır. |

## Kuyruklar nerede ve nasıl oluşturuluyor?

main.c önce gerekli donanımı hazırlar, osKernelInitialize çağırır ve defaultTask'ı oluşturur. Sonra main.c:139'da App_RTOS_Init çağrılır; main.c:147'de osKernelStart scheduler'ı başlatır. Yani uygulama görevleri yürümeye başlamadan kullanacakları nesneler hazırlanır.

:::source Src/app_rtos.c 43 59

App_RTOS_Init(void) parametre almaz, değer döndürmez. static initialized, ikinci çağrıda aynı nesnelerin yeniden oluşturulmasını engeller. Timestamp_Init cycle sayacını açıp ilerlemesini kontrol eder; RingBuffer_Init kayıtları ve sayaçları, Button_Init butonun başlangıç durumunu sıfırlar.

**Ortak FIFO'nun oluşturulduğu satır**, TxQueueHandle'a osMessageQueueNew sonucunun atandığı satırdır. Kaynakta `TxQueue[256]` diye bir uygulama dizisi görmemenizin nedeni, kuyruğun depolamasını RTOS'un bu çağrıyla oluşturmasıdır. Handle, o nesneye sonradan erişmek için saklanır. Bu nesnede ButtonTask ve TelemetryTask aynı boyutta TxItem_t öğeleri biriktirir.

| osMessageQueueNew parametresi | ButtonQueue çağrısı | TxQueue çağrısı | Ne demek? |
| --- | --- | --- | --- |
| msg_count | APP_BUTTON_QUEUE_LEN = 8 | APP_TX_QUEUE_LEN = 256 | En fazla kaç öğe bekleyebilir? |
| msg_size | sizeof(ButtonEvent_t) | sizeof(TxItem_t) | Her Put/Get kaç baytlık nesne kopyalar? |
| attr | &btn_q_attr | &tx_q_attr | Kuyruğa verilen ad gibi oluşturma bilgileri. & işareti nesnenin adresidir. |
| Dönüş | ButtonQueueHandle'a atanır | TxQueueHandle'a atanır | Sonraki çağrılarda kullanılacak kuyruk tanıtıcısı; başarısızlıkta NULL olabilir. |

`require(void *handle)` bu sonucu kontrol eden uygulama yardımcısıdır: NULL ise Error_Handler'a gider; değilse aynı tanıtıcıyı döndürür. Nesneyi require değil, osMessageQueueNew oluşturur. Error_Handler bu projede kesmeleri kapatıp sonsuz döngüde kalır.

:::source Src/app_rtos.c 67 74

CmdQueue aynı yöntemle CmdLine_t satır kopyaları için kurulur. `osSemaphoreNew(1, 0, &sem_attr)` ise en fazla 1 bildirim tutabilen, başlangıçta 0 bildirimli bir semaphore oluşturur. TxDoneSemHandle bunun tanıtıcısıdır. RunFlagsHandle bir event flag nesnesidir; RUN_BIT deney boyunca "üretime izin var" durumunu taşır.

## Görevler nereden doğuyor?

:::source Src/app_rtos.c 76 99

`osThreadNew(UartTxTask_Run, NULL, &uart_tx_attr)` bir görev oluşturur. İlk parametre görev giriş fonksiyonunun adresidir; fonksiyon adının yanında parantez olmaması burada önemlidir. Bu satır, normal bir C çağrısıyla UartTxTask_Run'a girip orada sonsuza kadar kalmak değildir. RTOS bu giriş fonksiyonunu, yığınını ve önceliğini görev olarak kaydeder.

İkinci parametre, giriş fonksiyonunun void *argument parametresine verilecek değerdir; bu projede NULL verilir ve görev içindeki `(void)argument` ile kullanılmadığı belirtilir. Üçüncü parametre ad, öncelik ve yığın boyutunu taşıyan attr adresidir. stack_size burada bayt cinsindedir. Dönüş görev tanıtıcısıdır; mevcut tek FIFO dalında require kontrolünden sonra kullanılmaz.

| Görev | Öncelik | CPU'yu bırakmasını sağlayan başlıca koşul |
| --- | --- | --- |
| TelemetryTask | AboveNormal, en yüksek uygulama görevi | RUN_BIT bekleme, osDelayUntil, dolu TX kuyruğunda bloklu Put |
| ButtonTask | Normal | Boş ButtonQueue'da Get, dolu TX kuyruğunda Put |
| UartTxTask | Low | Boş TxQueue'da Get, TxDoneSem'de Acquire |

Bu işlemci tek çekirdeklidir; normal görev kodlarından aynı anda biri CPU'da yürür. Kesmeler görev çalışmasını kesebilir. DMA ise donanım aktarımını CPU bir görevi çalıştırırken de ilerletebilir. CubeMX'in defaultTask'ı da Normal önceliktedir; bu nedenle aşağıdaki "ButtonTask çalışır" anlatımı diğer hazır görevleri dışlayan anında çalışma garantisi değildir.

## Bu yedi adım START'tan sonra gerçekleşir

:::source Src/app_tasks.c 119 127

START, kayıtları/sayaçları ve olay kimliğini sıfırlar; başlangıç ms'sini alır; önce g_running=true yapar, sonra RUN_BIT'i set eder. Çünkü bit set edilince yüksek öncelikli TelemetryTask çalışmaya uygun hâle gelebilir ve g_running'i okuyabilir. **Değeri hazırla, sonra o değeri okuyacak görevi uyandır** sırası burada kullanılır. Kuyruklar her START'ta yeniden oluşturulmaz; açılışta bir kez oluşturulmuştur.

# 1 | Kenarı kabul et ve RAM'de olay kaydı aç

Bu adımın çalışanı ButtonTask değil, **buton kesmesinden çağrılan uygulama kodudur**. Girişte bir elektriksel kenar vardır. Çıkışta, kabul edilmişse kimliği ve RAM slotu olan bir ButtonEvent_t oluşur. UART'a henüz mesaj gönderilmez.

## Uygulama fonksiyonuna kim geliyor?

:::source Src/app_hal_callbacks.c 55 60

HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) uygulamanın yazdığı callback'tir. Parametre, olayı bildiren GPIO pin maskesidir; metin olarak "PI11" veya düz sayı 11 değildir. USER_BUTTON_PIN, GPIO_PIN_11 makrosuna karşılık gelir. Yalnızca bu pin için Button_HandleEXTI çağrılır. Dıştaki IRQ/HAL yönlendirmesinin içini incelemiyoruz; uygulamaya gelen sözleşme bu parametredir.

Button_HandleEXTI(void) parametre almaz ve değer döndürmez. Buton pinini, deney durumunu ve zaman kaynaklarını kendi içinden okur. Fonksiyona hiç parametre gelmemesi, dış duruma bağımlı olmadığı anlamına gelmez.

Bu dosyanın static durumları s_last_edge_ms (son kenarın ms zamanı), s_edge_seen (daha önce kenar görüldü mü?) ve s_event_counter (kabul edilen olay sayacı) adlarını taşır. Button_Init(void) bunları açılışta sırasıyla 0, false ve 0 yapar. Button_ResetForScenario(void) yalnızca olay sayacını sıfırlar; START sırasında fiziksel kenar geçmişi temizlenmez.

:::source Src/app_button.c 24 41

1. Timestamp_Now sonucu t_entry'ye yazılır. Kenar kabul edilirse olayın t0'ı bu olacaktır. Filtreyi bitirdikten sonra değil, uygulama işleyicisine gelindiğinde alınır.
2. HAL_GetTick, debounce ve deney kronolojisi için ms sayacını verir. DWT cycle ile bu sayıyı birbirinden çıkarmayız; birimleri farklıdır.
3. quiet, önceki herhangi bir kenardan beri en az 30 ms sessizlik olup olmadığını hesaplar. İlk kenar için s_edge_seen=false olduğu için bu koşul geçer.
4. Son kenar zamanı her durumda güncellenir. Reddedilen sekmeler ve bırakma kenarları da sessizlik hesabının yeni başlangıcıdır.
5. pressed, pinin şu an aktif yüksek basılı seviyede olup olmadığını söyler. Sessizlik yoksa, pin basılı değilse veya g_running false ise return ile çıkılır.
6. Kabul edilen kenarda s_event_counter artırılır; kimlik event_id'ye alınır. g_cfg.scenario_id kopyalanır. t0_abs_ms = now_ms - g_run_start_ms hesaplanır.

`!quiet`, "quiet yanlışsa"; `||`, "koşullardan en az biri doğruysa" anlamındadır. Filtre 30 ms boyunca bu fonksiyonun içinde beklemez. Örneğin son kenar 100 ms, bu kenar 115 ms ise sessizlik 15 ms'dir ve kenar reddedilir. Sırf daha sonra 30 ms doldu diye geriye dönük olay üretilmez; yeni kenar gerekir.

## Timestamp_Now nasıl kayıt alıyor?

:::source Inc/app_timestamp.h 18 22

Parametresi yoktur. uint32_t türünde o anki DWT cycle sayacını döndürür. `uint32_t t_entry = Timestamp_Now();` demek, dönen 32 bit değeri yerel t_entry değişkenine yazmaktır. Timestamp_Now ring kaydını kendiliğinden değiştirmez. **Zamanı okumak ile RAM'deki EventRecord_t'ye yazmak ayrı satırlardır.**

216 MHz'de 216 cycle, 1 us eder. Bu belgede izleyeceğimiz olay 7 için öğretici t0=648000000 cycle seçiyoruz. Senaryo etiketi 2, t0_abs_ms=1500 olsun. Bunlar kart ölçümü değildir. t0 gerçek fiziksel kenarın donanım yakalaması değil, Button_HandleEXTI'nin başında okunan yazılım damgasıdır.

## RAM kaydını açan kodun tamamı

:::source Inc/app_ring_buffer.h 34 42

EventRecord_t içindeki t0..t4, ham cycle alanlarıdır. t0_abs_ms deney kronolojisini; event_id ve scenario_id kimliği; status sonuç durumunu tutar. ts_mask hangi t1..t4 alanlarının gerçekten ölçüldüğünü belirtir. valid ise bu slotta ayrılmış bir olay bulunup bulunmadığıdır. Bir damga sıfır olabilir; sıfır değeri tek başına "ölçülmedi" anlamına gelmediği için ayrı maske kullanılır.

:::source Src/app_ring_buffer.c 8 10

s_records zaten bellekte ayrılmış 256 elemanlı static bir dizidir. "RAM kaydı açmak" her basışta yeni malloc yapmak veya diskte dosya açmak değildir; **bu dizide bir slot seçip alanlarını yeni olaya göre doldurmaktır**.

:::source Src/app_ring_buffer.c 28 49

| RingBuffer_Alloc parametresi / sonucu | Nereden geliyor? | Bu örnekte | Anlamı |
| --- | --- | --- | --- |
| event_id | Button_HandleEXTI'nin artırdığı sayaç | 7 | Olayın kimliği. |
| scenario_id | g_cfg.scenario_id | 2 | Deney etiketi. |
| t0 | Yerel t_entry | 648000000 | Hassas başlangıç damgası, cycle. |
| t0_abs_ms | now_ms - g_run_start_ms | 1500 | Deney başlangıcından beri ms. |
| Dönüş: uint16_t idx | Seçilen dizi indeksi | 6 | Sonraki görevlerin aynı kaydı bulacağı yer. |

İlk altı olay ayrılmışsa s_total_allocated=6'dır. `idx = 6 % 256` sonucu 6 olur. `%`, bölmenin kalanıdır; dizide 255'ten sonra tekrar 0'a dönmeyi sağlar. Toplam sayı 256'ya ulaştıktan sonra yeni ayırmalar eski kayıtların üstüne yazar ve ring_overflow_count artar. Ardından toplam ayırma sayısı artırılır.

`EventRecord_t *r = &s_records[idx];`, r işaretçisini seçilen gerçek dizi öğesine yöneltir. Yeni bir kayıt kopyası oluşturmaz. `memset(r, 0, sizeof(*r))` parametreleri sırasıyla **başlangıç adresi**, her bayta yazılacak 0 değeri ve sıfırlanacak EventRecord_t boyutudur. Eski t1..t4, maske ve diğer alanlar temizlenir.

`r->event_id = event_id` gibi satırlar o slotun alanlarını yazar. `->`, işaretçinin gösterdiği yapının alanına erişimdir. status PENDING olur: "olay var, sonucu henüz yok". valid true olur: "bu slotta bir olay var". t0 yazılmıştır; t1..t4 için ölçüldü bitleri henüz set edilmez.

`taskENTER_CRITICAL_FROM_ISR()` ile korumaya girilir ve önceki maske durumu saved içinde saklanır; `taskEXIT_CRITICAL_FROM_ISR(saved)` ile geri yüklenir. Amaç, paylaşılan slotun seçilip hazırlanması sırasında ilgili eşzamanlı erişimlerin araya girmemesidir. Kernelin iç algoritmasını açmadan burada gereken bilgi budur. Bu bölgenin içinde queue beklemesi veya UART gönderimi yoktur.

## Slot bilgisi olaya nasıl bağlanıyor?

:::source Src/app_button.c 43 49

Bu yapı başlatıcısı bir ButtonEvent_t oluşturur. `.t0 = t_entry` bir alanı doldurur; `.ring_index = RingBuffer_Alloc(...)` ise önce fonksiyonu çağırır, onun döndürdüğü indeksi alana yazar. Böylece örnekte evt.event_id=7 ve evt.ring_index=6 birlikte yolculuk eder.

**Neden kayıt queue'ya teslimden önce açıldı?** ButtonQueue dolu çıkarsa bile kabul edilmiş basışın DROP olarak kaydedilebilmesi için. Queue başarısızlığı, RAM kaydının hiç yokmuş gibi kaybolmasını gerektirmez. Bir sonraki adım tam bu teslimi yapar.

:::snapshot 1

# 2 | ButtonQueue'ya kopyala; ButtonTask'ın çalışmasını bekle

Bu adım iki bağlamı birleştirir: ISR olayı teslim eder; ButtonTask daha sonra alır. "Kopyala" diyen uygulama çağrısı osMessageQueuePut, "al" diyen çağrı osMessageQueueGet'tir. İşlemi gerçekleştiren RTOS, kopya boyutunu kuyruğun oluşturulduğu anda verilen sizeof(ButtonEvent_t)'den bilir.

:::source Inc/app_rtos.h 20 27

Queue'nun öğe tipi budur. t0, t0_abs_ms ve kimlikler değer olarak taşınır. ring_index, s_records dizisindeki slot numarasıdır; kaydın bütün t0..t4 içeriği bu pakete konmaz. RAM kaydının kendisini kopyalamak yerine sonraki aşamaların aynı kaydı bulmasına yetecek bağlantı taşınır. Bu yapıda işaretçi alanı yoktur; içindeki sayısal değerler kuyruk kopyasıyla bağımsız kalır.

## Put çağrısındaki dört parametre

:::source Src/app_button.c 51 54

| Parametre | Gerçek ifade | İşlevi |
| --- | --- | --- |
| mq_id | ButtonQueueHandle | Hangi oluşturulmuş kuyruk kullanılacak? |
| msg_ptr | &evt | Kopyalanacak ButtonEvent_t nesnesinin başlangıç adresi. |
| msg_prio | 0u | Mesaj önceliği alanı. Burada 0; projedeki uyarlama bu alanı kullanmıyor. |
| timeout | 0u | Yer yoksa bekleme; hemen sonuç ver. ISR'da beklemeye izin yok. |
| Dönüş: osStatus_t | osOK ile karşılaştırılır | Başarılı teslim mi, yoksa başarısızlık mı? |

`&evt` gönderilince kuyrukta yalnızca evt'nin adresi saklanmıyor. Başarılı Put, o adresteki ButtonEvent_t içeriğini kuyruk depolamasına kopyalar. ISR'daki yerel evt'nin ömrü bitse de kuyruktaki kopya vardır. Yapıda ham t0, t0_abs_ms, kimlikler ve indeks bulunur; bunlar değer olarak kopyalanır. Böylece görev sonradan ISR'ın geçici belleğine erişmek zorunda kalmaz.

:::diagram copy-event

Kopyalamanın iç bellek hareketini uygulama ayrıca memcpy ile yazmaz; **bu işi uygulamanın isteği üzerine osMessageQueuePut yapar**. Burada kullanılan RTOS çağrısının sözleşmesini bilmek yeterlidir. Kuyrukta yer yoksa osOK dönmez; RingBuffer_CloseDropIsr(evt.ring_index,event_id) aynı slotu DROP yapar ve btn_queue_drop_count artırır. Başarısız Put için "olay görev tarafından mutlaka işlenecek" diyemeyiz.

## ButtonTask nerede bekliyordu?

:::source Src/app_tasks.c 216 225

ButtonTask_Run(void *argument) RTOS tarafından başlatılan görev girişidir. argument'a oluşturma sırasında NULL verilmiştir; kullanılmaz. `for (;;)` görev döngüsüdür. Her turda ButtonEvent_t evt için yerel alan vardır ve Get, sıradaki öğeyi buraya yazacaktır.

| Get parametresi | Gerçek ifade | Açıklama |
| --- | --- | --- |
| mq_id | ButtonQueueHandle | ISR'ın yazdığı kuyruk. |
| msg_ptr | &evt | Alınan öğenin kopyalanacağı, ButtonTask'a ait yerel nesne. ISR'ın evt'siyle aynı değişken değildir. |
| msg_prio | NULL | Mesaj önceliğini ayrıca almak için bir çıktı adresi vermiyoruz. |
| timeout | osWaitForever | Kuyruk boşsa veri gelene kadar bloklu bekle. |
| Dönüş | osOK | Öğe alındı. Diğer sonuçta continue ile döngü başına dönülür. |

Kuyruk boşken görev sürekli bu satırı tekrar tekrar çalıştırmaz. RTOS, ButtonTask'ı **Blocked / bekliyor** durumuna alır; CPU başka hazır göreve verilir. Get koşulu karşılanınca görev beklemekten çıkar. Kuyruk zaten doluysa Get'in veri beklemek için bloklanmasına gerek yoktur.

## "Uygun olduğunda" hangi kuralla uygun olur?

Görevin kullanılabilir işi olması ve CPU sırasının gelmesi ayrı koşullardır. FreeRTOSConfig.h'ta preemption açıktır. Genel kural, o anda çalışabilir görevler arasından en yüksek önceliğe sahip olanın seçilmesidir. Kesmeler ve aynı öncelikteki diğer görevler de zamanlamaya etki eder. ButtonQueue'ya teslim, ButtonTask'ın önceliğini değiştirmez.

| Örnek an | ButtonTask durumu | CPU'da ne olabilir? | Neden? |
| --- | --- | --- | --- |
| Kuyruk boş | Blocked | TelemetryTask veya başka hazır görev | ButtonTask'ın yapacağı yeni olay yok. |
| ISR olayı teslim etti | Ready | Hâlâ ISR, sonra yüksek öncelikli TelemetryTask | Hazır olmak hemen çalışmak değildir. |
| TelemetryTask yük döngüsünü bitirdi ama çalışabilir | Ready | TelemetryTask | Higher priority görev hâlâ hazır. |
| TelemetryTask osDelayUntil ile bloklandı | Ready -> Running olabilir | ButtonTask | Daha yüksek öncelikli hazır görev kalmadıysa scheduler seçer. |
| ButtonTask yeni olay beklemeye döndü | Blocked | UartTxTask gibi daha düşük öncelikli görev | CPU bekleyen işe ayrılabilir. |

ISR sırasında düşük öncelikli UartTxTask çalışıyorduysa, olayla hazır olan ButtonTask kesme çıkışındaki zamanlama noktasında onun önüne geçebilir. ISR sırasında TelemetryTask çalışıyorduysa aynı sonuç zorunlu değildir: TelemetryTask daha yüksek önceliklidir. "Olay geldi -> ButtonTask çağrıldı" şeklinde doğrudan fonksiyon çağrısı yoktur.

## t1 nasıl alınır, ne zaman ring'e yazılır?

Başarılı Get çağrısından dönüldükten sonra `uint32_t t1 = Timestamp_Now();` çalışır. DWT'den okunan cycle değeri **ButtonTask'ın yerel t1 değişkenine** atanır. Öğretici örnekte t1=648008640 cycle; fark 8640 cycle yani 40 us'tur.

Bu satır henüz `s_records[6].t1` yazmaz. Ring kaydına t1 ve t2, biraz sonra RingBuffer_SetT1T2 ile birlikte yazılacak. Önceki kısa anlatımdaki "t1'i kaydeder" ifadesinin tam karşılığı budur: önce değeri yerel değişkende tutar; kalıcı olay kaydını ayrı fonksiyonda günceller. Bu fark, ölçüm anıyla kayıt yazma maliyetini ayırmak için önemlidir.

:::snapshot 2

# 3 | Protocol_BuildBTN ile 64 baytı gerçekten oluştur

ButtonTask artık olayın yerel kopyasına ve t1'e sahiptir. Sıradaki işi, bu olaydan bir TxItem_t hazırlamaktır. TxItem_t'nin tamamı 64 bayt değildir: **içindeki frame dizisi 64 bayttır**; kimlik ve tür alanları ayrıca vardır. UART'a yalnızca frame gönderilir, yapının bütün bellek düzeni gönderilmez.

:::source Inc/app_rtos.h 29 35

:::source Src/app_tasks.c 229 234

type alanına MSG_TYPE_BTN yazılır. event_id ve ring_index, evt'den taşınır. Bu iki alan UART'tan çıkacak baytların dışında, gönderim sonucunu doğru RAM kaydına bağlamak için öğenin üzerinde kalır. item.frame ise aşağıdaki fonksiyonların dolduracağı çıkış dizisidir.

## Önce cycle farkı mikrosaniyeye çevrilir

:::source Src/app_timestamp.c 20 28

Timestamp_DeltaToUs(uint32_t from,uint32_t to) başlangıç ve bitiş cycle değerlerini alır, uint32_t türünde tam mikrosaniye döndürür. `to - from` işaretsiz farktır. `SystemCoreClock / 1000000` bu projede 216 verir; sonuç fark/216'dır. Tamsayı bölmesi kesirli mikrosaniyeyi atar. Bölen sıfır hesaplanırsa sıfıra bölmemek için 1 yapılır. Gerçek aralık 32 bit sayacın bir çevriminden kısa olduğu sürece işaretsiz çıkarma sayaç başa dönse de kullanılabilir.

Bizim çağrımız `Timestamp_DeltaToUs(evt.t0,t1)` olduğundan 648008640 - 648000000 = 8640 cycle -> 40 us çıkar. Protocol_BuildBTN'nin son parametresine ham t1 değil, **t1-t0'ın mikrosaniye değeri** verilir.

## Protocol_BuildBTN'nin parametre haritası

:::source Src/app_protocol.c 91 100

| Parametre | Çağrıdaki karşılığı | Örnek değer | Görevi |
| --- | --- | --- | --- |
| out[APP_FRAME_SIZE] | item.frame | 64 baytlık çıktı alanının adresi | Fonksiyon sonucu bu diziye yazar; yeni dizi döndürmez. |
| event_id | evt.event_id | 7 | BTN mesajında hangi olay olduğu. |
| scenario_id | evt.scenario_id | 2 | Deney etiketi. |
| t0_abs_ms | evt.t0_abs_ms | 1500 | Basışın deney içinde kaçıncı ms'de olduğu. |
| d1_us | Timestamp_DeltaToUs(evt.t0,t1) | 40 | Olaydan ButtonTask'a ulaşma süresi, us. |
| Dönüş: bool | Çağıran (void) ile bırakır | true | İçerik sığıp normal çerçeve oluşturuldu mu? |

C'de fonksiyon parametresindeki out[64] yazımı, bu kullanımda dizinin ilk elemanına işaret eden parametre olarak davranır; 64 baytlık ikinci bir dizinin fonksiyon içine otomatik kopyası değildir. item.frame adresini verdiğimiz için out üzerinden yazılan baytlar doğrudan item.frame'e gider. Kodun güvenli kullanımı, çağıranın gerçekten 64 baytlık yer vermesine dayanır.

## snprintf hangi metni, nereye yazıyor?

Fonksiyon içindeki `char content[APP_FRAME_SIZE];` geçici bir metin tamponudur. snprintf'in ilk parametresi bu hedef, ikinci parametresi sizeof(content)=64 kapasitesi, üçüncü parametresi biçim dizesidir. Sonraki parametreler, biçimdeki yer tutucuları soldan sağa doldurur.

| Biçimin parçası | Hangi argüman? | Oluşan metin |
| --- | --- | --- |
| BTN, | Sabit metin | BTN, |
| %u | (unsigned)event_id | 7 |
| ,%u | (unsigned)scenario_id | ,2 |
| ,%lu | (unsigned long)t0_abs_ms | ,1500 |
| ,%lu | (unsigned long)d1_us | ,40 |
| ,PRESSED | Sabit metin | ,PRESSED |

Ortaya `BTN,7,2,1500,40,PRESSED` çıkar. Metin **23 karakterdir**; content içinde sonunda ayrıca NUL bulunur. snprintf'in döndürdüğü n=23, bu son NUL'u saymaz. `%u` ve `%lu` için yapılan tür dönüşümleri, biçim dizgesinin beklediği argüman türünü sağlar. Bu metin henüz 64 baytlık tel çerçevesi değildir: dolgu, CRC ve LF henüz eklenmedi.

## finish_frame neden ayrı ve ne alıyor?

:::source Src/app_protocol.c 35 49

Çağrı `finish_frame(out, content, n, "BTN")` şeklindedir. out hedef frame adresi, content hazırlanmış metnin adresi, n snprintf'in bildirdiği uzunluk, type_tag ise hata mesajında kullanılacak "BTN" etiketidir. Dönüş bool'dur. Geçerli durumda true; taşmada hata çerçevesi oluşturup false döner.

`n >= 0 && (size_t)n <= 62` koşulu, biçimlendirme sonucunun geçerli ve payload'a sığmış olmasını ister. Karar strlen'den değil, snprintf'in **sığsaydı üreteceği uzunluğu** bildiren n değerinden verilir. Bu sayede geçici tamponda kesilmiş bir metin yanlışlıkla tam mesaj sanılmaz.

Normal dal pack_payload çağırır. Sığmama durumunda ERR,ENCODE_OVERFLOW,BTN,... içeriği hazırlanıp paketlenir. Çağıran `(void)` ile bool sonucu kullanmasa bile out dizisi yine doldurulmuştur; bu durumda gönderilecek içerik ERR olur. Kod bool'u ayrıca TX_ERR kaydına dönüştürmez.

## pack_payload dört satırda çerçeveyi nasıl bitirir?

:::source Src/app_protocol.c 23 29

| Satır / çağrı | Parametreler bu örnekte | Bellekte yapılan işlem |
| --- | --- | --- |
| memcpy(out, content, len) | Hedef frame; kaynak geçici metin; len=23 | 0..22 indislerine 23 metin baytı kopyalanır. NUL kopyalanmaz. |
| memset(out + len, ' ', 62 - len) | Hedef frame+23; değer ASCII boşluk; sayı 39 | 23..61 indisleri 0x20 ile doldurulur. |
| Protocol_Crc8(out, 62) | Payload başlangıcı; toplam 62 bayt | Metin ve dolgu üzerinden CRC bulunur, out[62]'ye yazılır. |
| out[63] = '\n' | Sabit 0x0A | Son bayt LF olur. |

out+len işaretçi aritmetiğidir: dizinin başlangıcından len bayt ileri gider. memcpy bir kaynağın mevcut baytlarını kopyalar; memset ise verilen tek bayt değerini belirtilen sayıda tekrar yazar. Burada ikisi farklı işler yapar.

## CRC fonksiyonunun kullanılan işlemleri

:::source Src/app_protocol.c 8 19

Protocol_Crc8(const uint8_t *data,size_t len), baytların adresini ve sayısını alır; 8 bit kontrol değeri döndürür. crc=0 ile başlar. Her bayt crc ile XOR edilir. Her bayt için sekiz kez, üst bit set ise sola kaydırılmış değere 0x07 XOR edilir; değilse sadece sola kaydırılır. uint8_t dönüşümü değeri sekiz bitte tutar. Ayrıntılı polinom teorisi gerekmiyor; gönderilen ilk 62 baytın hepsi aynı kuralla işleniyor.

Dolgu CRC'den önce yazılır, çünkü CRC'ye dolgu baytları da dahildir. CRC çıkış çerçevesinde iki harfli hex metni değil **tek ham bayttır**. Aşağıdaki bayt gezgininde 64 konumun tamamı, içerik/dolgu/CRC/LF rolleriyle gösterilir. Boşluklar görünür olsun diye bir nokta ile temsil edilir; gerçek değeri 0x20'dir.

:::frame

Bu çağrı zinciri `Protocol_BuildBTN -> snprintf -> finish_frame -> pack_payload -> Protocol_Crc8` şeklindedir. Hiçbiri UART'a göndermez. Yalnızca RAM'deki item.frame dolmuştur. t2 henüz alınmadı; t4 ise gönderim gerçekleşmediği için henüz bilinemez. Bu yüzden canlı BTN'nin alanlarında t2 ve t4 bulunmaz.

:::snapshot 3

# 4 | t2'yi al, TxQueue'ya teslim et ve FIFO sırasını bekle

Bu adımda hâlâ ButtonTask çalışıyor. item.frame hazırdır. Şimdi yanıt hazırlama süresinin bitişi alınır ve hazır iş ortak gönderim kuyruğuna konur. **Queue'ya teslim, UART'ın göndermesi veya bilgisayarın mesajı alması değildir.**

:::source Src/app_tasks.c 236 236

t2 = Timestamp_Now(), cycle sayacını yerel t2 değişkenine yazar. Öğretici örnekte t2=648025920 olsun: t0'dan 120 us, t1'den 80 us sonradır. Damga TX Put öncesindedir. Put dolu kuyrukta yer bekletirse bu bekleme yanıt hazırlama aralığına değil, t2'den sonraki aralığa girer.

## Ortak FIFO'ya hangi nesne kopyalanır?

:::source Src/app_tasks.c 247 255

| Parametre / sonuç | İfade | Anlamı |
| --- | --- | --- |
| Kuyruk | TxQueueHandle | App_RTOS_Init'in oluşturduğu 256 öğeli ortak kuyruk. |
| Kaynak adres | &item | Yalnızca frame değil, bütün TxItem_t'nin kopyalanacağı adres. |
| Mesaj önceliği | 0u | Görev önceliği değildir; bu kaynakta FIFO'yu değiştirmez. |
| Bekleme | APP_TX_QUEUE_SEND_TIMEOUT_MS = 200 | Kuyruk doluysa en çok 200 tick, burada yaklaşık 200 ms bekleme. |
| Sonuç | osStatus_t st | Teslim başarılıysa osOK; başarısızsa DROP yoluna gidilir. |

Kopya boyutu queue kurulurken sizeof(TxItem_t) verilerek belirlenmiştir. Bu yüzden type, event_id, ring_index ve frame[64] aynı öğede taşınır. Derleyicinin yapı içi hizalama boşlukları varsa boyut hesabı bunları da kapsar. Uygulama kopyayı tek tek alan atayarak yapmaz; Put bunu kuyruk sözleşmesi kapsamında gerçekleştirir.

Kuyrukta boş yer varsa çağrı veri kopyalanınca döner. Yoksa ButtonTask bloklanabilir. **Bloklanmak, CPU'da 200 ms boş döngü çevirmek değildir.** Daha düşük öncelikli UartTxTask kuyruktan öğe alıp yer açabilir. Bekleme sınırı içinde teslim olamazsa st osOK olmaz ve RingBuffer_CloseDropTx kaydı DROP yapar. Deadline olan 20 ms ile bu 200 ms bekleme sınırı farklı amaçlardır.

## Yerel t1/t2 şimdi ring'e nasıl yazılıyor?

:::source Src/app_ring_buffer.c 67 77

| RingBuffer_SetT1T2 parametresi | Bu çağrıdaki değer | Görevi |
| --- | --- | --- |
| ring_index | evt.ring_index = 6 | s_records içinde hangi slot? |
| event_id | evt.event_id = 7 | Slotun hangi olaya ait olması gerekiyor? |
| t1 | Yerel t1 = 648008640 | Daha önce alınmış damga. Yeniden zaman okunmaz. |
| t2 | Yerel t2 = 648025920 | Daha önce alınmış hazırlama bitişi. |
| Dönüş | void | Güncelleme yapar; yeni kayıt döndürmez. |

Kritik bölgeye girilir, slot_for(6,7) ile doğru slot bulunur. İşaretçi NULL değilse r->t1 ve r->t2 atanır. ts_mask, T1 ve T2 bitleriyle OR'lanır; örnekte 0b0000 -> 0b0011 olur. Bu, zamanların artık gerçekten alındığını işaretler. t0, kayıt açılırken zaten yazılmıştı.

:::source Src/app_ring_buffer.c 52 65

slot_for(uint16_t ring_index,uint16_t event_id), dizi indeksini ve beklenen kimliği alır; uygun EventRecord_t'nin adresini veya NULL döndürür. İndeks sınır dışında, kayıt geçersiz ya da kimlik farklıysa id_mismatch_count artar. Bunun nedeni dairesel tamponun aynı fiziksel slotu ileride yeniden kullanabilmesidir. İndeks 6 artık başka olaya aitse olay 7'nin gecikmiş yazması ona zarar vermesin diye kontrol yapılır. Bu fonksiyon kendi kritik bölgesini açmaz; çağıranı koruma içinde olmalıdır.

**Neden kayıt yazma Put'tan sonra?** t2, Put öncesinde alınarak yanıtın hazırlanma bitişi sabitlenir. Kayıt güncellemesinin maliyeti t1-t2 aralığına eklenmez. Ama bu maliyet bütünden tamamen silinmiş değildir; sonraki adımların zamanına etki edebilir. Damga alma sırası ile kayıt yazma sırasını bu yüzden ayrı anlatıyoruz.

Başarılı Put sonrasında ButtonTask, UartTxTask'tan yüksek öncelikli olduğu için bu kısa kayıt güncellemesini göndericinin aynı öğeyi işlemesinden önce tamamlar. TelemetryTask araya girebilir; o da UartTxTask'tan yüksek önceliklidir. Ardından ButtonTask yeni olay beklemek üzere kendi döngüsüne döner; UART bitişini burada beklemez.

## FIFO'daki TEL mesajları nereden geldi?

TelemetryTask, periyodik olarak kendisine ait TxItem_t üretir. type TEL, event_id 0, ring_index RING_INDEX_NONE olur. Son işaret, o mesajın kapatılacak bir buton kaydı olmadığını belirtir. Protocol_BuildTEL kendi frame alanını doldurur. Bu üretici de aynı TxQueueHandle'a Put yapar.

:::source Src/app_tasks.c 300 306

:::source Src/app_tasks.c 316 320

Protocol_BuildTEL parametreleri sırayla çıktı frame adresi, artan sıra numarası, senaryo etiketi, deney içi ms, periyot, iterasyon sayısı, ölçülmüş yük süresi ve sayaç kopyasının adresidir. Bu çağrı BTN'nin önüne kendiliğinden geçmez; yalnızca kendi üretildiği anda kuyrukta sıraya girer. RingBuffer_GetCounters sayaçları kopyalar; TEL bu kopyanın değerlerini metne çevirir.

TelemetryTask'ın yapay yükü ise aşağıdaki CPU döngüsüdür. load_iter kadar tur döner; volatile acc işlemlerin tamamen silinmesini önler. Bu sırada görevin yüksek önceliği daha düşük öncelikli hazır görevlerin CPU'yu almasını geciktirebilir.

:::source Src/app_tasks.c 292 298

Periyodun uyku noktası osDelayUntil(wake)'tir. Parametre "kaç ms uyu" değil, ulaşılacak mutlak kernel tick değeridir. Görev bu tarihe kadar bloklanınca daha düşük öncelikli görevlerin önü açılır. Hedef kaçırılmışsa kod wake'i now+period yaparak yine bir bekleme sağlar. Sonsuz telafi döngüsüyle CPU'yu sürekli elinde tutmaz.

## Görev önceliği neden FIFO sırasını değiştirmez?

Scheduler, **çalışabilir görevlerden** hangisinin CPU'da yürüyeceğini seçer. TxQueue ise **başarıyla eklenmiş mesajların** sırasını tutar. Bunlar farklı listeler ve farklı kararlardır. ButtonTask'ın Normal, UartTxTask'ın Low olması, ButtonTask tarafından daha sonra eklenen BTN'yi kuyruğun başına taşıyan bir emir değildir.

Örnek sırada TEL1, TEL2, BTN7 olsun. UartTxTask kuyruktan sırayla TEL1, TEL2, BTN7 alır. BTN7'yi üreten görevin önceliği kuyruk içindeki bu sıralamayı değiştirmez. Bu kaynakta Put'un msg_prio alanı 0'dır ve paketle gelen CMSIS-FreeRTOS uyarlaması mesaj önceliği alanını göz ardı eder.

:::fifo

Bu gezgindeki üç öğe, öğretici örneğin bekleyen öğeleridir; gerçek kuyruk kapasitesi 3 değil 256'dır. Bir öğeyi Get ile almak, çerçevenin gönderildiği anlamına gelmez: öğe artık görevde, gönderim hâlâ sıradaki iştedir.

:::snapshot 4

# 5 | UartTxTask CPU'yu alır, baytları kopyalar ve DMA'yı başlatır

TxQueue'da mesaj olması UartTxTask'ı çalışabilir yapabilir; CPU'yu alması için scheduler'ın onu seçmesi gerekir. Low öncelikli bu görev, TelemetryTask veya ButtonTask gibi daha yüksek öncelikli **hazır** görevler varken onların önüne geçmez. Bu görevler beklemeye geçince veya başka nedenle çalışabilir olmadığında gönderici ilerleyebilir.

## UartTxTask'ın gerçek alma noktası

Ana döngü önce CmdQueue'da bekleyen komutları timeout=0 ile alır ve işler. Sonra mevcut tek FIFO dalındaki aşağıdaki kod TxQueue'ya bakar.

:::source Src/app_tasks.c 205 211

`osMessageQueueGet(TxQueueHandle,&item,NULL,20u)` sıradaki öğeyi UartTxTask'ın yerel TxItem_t item nesnesine kopyalar ve kuyruktan çıkarır. NULL, mesaj önceliği için çıktı adresi istemediğimizi söyler. 20 tick, kuyruk boşsa bekleme sınırıdır. Veri yokken sonsuza kadar burada kalmamak, görev döngüsünün yeniden CmdQueue'ya bakabilmesini sağlar. Bu sayı tüm komutların en geç 20 ms'de uygulanacağı garantisi değildir; görev CPU bekleyebilir veya etkin bir aktarım/dump içinde olabilir.

Get başarılıysa `send_item(&item)` normal C fonksiyon çağrısıdır. Yeni görev oluşturmaz; hâlâ UartTxTask bağlamında çalışır. `const TxItem_t *item` parametresi, alınan öğenin adresidir. const bu işaretçi üzerinden öğeyi değiştirmemeyi ifade eder.

:::source Src/app_tasks.c 56 59

send_item içinde iki yerel sayı t3 ve t4 sıfırlanır. `uart_tx_send(item->frame,&t3,&t4)` hazır 64 baytın adresini ve damgaların yazılacağı iki değişkenin adresini verir. İsimler fonksiyonlara göre aynı olsa da içerideki işaretçi parametreleri ile çağıranın sayısal değişkenlerini ayıralım.

| uart_tx_send parametresi | Çağrıdaki ifade | Nasıl kullanılıyor? |
| --- | --- | --- |
| const uint8_t frame[64] | item->frame | Kaynak 64 baytın başlangıcına erişim. Bu fonksiyon üzerinden kaynak değiştirilmez. |
| uint32_t *t3 | &t3 | send_item'ın yerel t3 değişkeninin adresi. *t3=... bu değişkeni doldurur. |
| uint32_t *t4 | &t4 | send_item'ın yerel t4 değişkeninin adresi. Başarılı bitişte sonuç yazılır. |
| Dönüş: TxResult_t | r değişkenine alınır | TX_OK, TX_START_ERR, TX_DMA_ERR veya TX_TIMEOUT. |

**Adres vermek**, fonksiyonun çağıranın değişkenine sonuç yazabilmesini sağlar. Örneğin uart_tx_send içindeki `*t3 = Timestamp_Now();`, işaretçinin tuttuğu adresi değiştirmez; o adreste duran sayıyı değiştirir. Böylece fonksiyon tek enum sonucu döndürürken iki zaman damgasını da çıktı parametreleriyle iletir.

## Kalıcı s_tx_buffer nerede ve neden?

:::source Src/app_tasks.c 19 22

Bu dizi dosya düzeyinde static'tir; uygulama boyunca RAM'de yaşar ve adı bu .c dosyasına özeldir. **Kalıcı burada güç kesilince saklanır anlamına gelmez**; bir yerel fonksiyon çağrısının bitişiyle ömrü sona ermez anlamındadır. __attribute__((aligned(32))) adresin 32 bayt sınırına hizalanmasını ister. 64 baytlık alan, gerekirse cache temizliği için iki 32 baytlık satıra denk gelir.

DMA, başlatma fonksiyonu döndükten sonra da bellekteki baytları okur. UartTxTask bu diziye yeni mesajı, önceki aktarım tamamlanmadan veya zaman aşımı sonrası iptal edilmeden yazmaz. Tek sahipli gönderim düzeniyle tamponun ömrü ve yeniden kullanım sırası birlikte yönetilir.

:::source Src/app_tasks.c 27 43

`memcpy(s_tx_buffer, frame, APP_FRAME_SIZE)` parametreleri hedef adres, kaynak adres ve kopyalanacak bayt sayısıdır. Burada 64 bayt, UartTxTask'ın aldığı öğenin frame alanından s_tx_buffer'a kopyalanır. Queue'dan alma sırasında daha önce yapılan kopyadan farklı, ikinci bir kopyadır. İlk kopya **işi görevler arasında taşımak**, bu kopya **DMA'nın okuyacağı tek gönderim alanını hazırlamak** içindir.

:::diagram copy-tx

Koşullu cache bloğu, veri önbelleği varsa ve açık ise bu alanı RAM'e yansıtır. Böylece DMA'nın okuyacağı bellek hazırlanır. Mevcut main.c yalnızca I-Cache'i açar; bu kod D-Cache açık kullanım için de bir kontrol içerir. Burada HAL/cache altyapısının içini incelemiyoruz; uygulama sırasındaki amacı, DMA başlamadan veriyi hazır etmektir.

Sonraki while, TxDoneSem'de önceki gönderim/zaman aşımı yolundan kalmış jetonu varsa tüketir. Acquire timeout=0 olduğu için jeton yokken bloklanmaz. Döngü eski bildirimi temizledikten sonra g_tx_dma_error=false yapılır; yeni aktarım öncekinin hata bayrağıyla başlamaz.

## t3 tam olarak hangi satırda ve hangi birimde?

`*t3 = Timestamp_Now()` kopyalama, cache kontrolü ve eski bildirim temizliği sonrasındadır. Öğretici örnekte 648388800 cycle olsun; t0'dan 1800 us sonradır. T3 henüz ring kaydına yazılmıyor, send_item'ın yerel t3 değişkenine yazılıyor. UART başlatma çağrısının hemen öncesini ölçüyor; ilk fiziksel bitin çıktığı anın donanım kaydı değildir.

`HAL_UART_Transmit_DMA(&APP_UART_HANDLE,s_tx_buffer,APP_FRAME_SIZE)` bu uygulamada **huart1 UART nesnesinin adresini**, gönderilecek **s_tx_buffer adresini** ve **64 bayt uzunluğu** alır. APP_UART_HANDLE makrosu huart1'e açılır. HAL_OK dönerse başlatma kabul edilmiştir; bu, son bitin gönderildiği anlamına gelmez. Diğer sonuçta TX_START_ERR ile çıkılır.

## TxDoneSem'de bloklanmak ne demek?

:::source Src/app_tasks.c 45 54

`osSemaphoreAcquire(TxDoneSemHandle,100)` gönderim sonucu bildirimini bekler. İlk parametre semaphore nesnesinin tanıtıcısı, ikinci parametre maksimum bekleme tick sayısıdır. Jeton henüz yoksa UartTxTask **Blocked** durumuna geçer; CPU başka göreve verilebilir. Donanım aktarımı bu sırada devam eder. Kod her baytı CPU döngüsüyle tek tek beklemiyor.

Bildirim Acquire çağrısından önce gelmişse semaphore jetonu tutmuş olabilir ve Acquire hemen başarılı dönebilir. Bu yüzden "mutlaka önce uyur sonra callback gelir" sırası zorunlu değildir; mekanizma erken gelen bildirimi de taşıyabilir. Jetonun içinden t4 verisi çıkmaz: semaphore yalnızca bildirimdir, sayı g_tx_done_cycles içinde taşınır.

Bekleme osOK dönmezse HAL_UART_AbortTransmit ile aktarımın iptali istenir ve TX_TIMEOUT döner. osOK dönerse g_tx_dma_error ayrıca kontrol edilir; çünkü hata callback'i de aynı semaphore'u bırakır. Hata varsa TX_DMA_ERR, yoksa *t4=g_tx_done_cycles ve TX_OK olur. Callback'in bu iki alanı nasıl hazırladığı sıradaki adımdadır.

:::snapshot 5

# 6 | Callback t4'ü alır; görev uyanınca aynı kaydı kapatır

Bu adımda iki farklı yürütme zamanı vardır. Önce UART tamamlanma kesmesinde callback çalışır. Sonra, scheduler uygun bulduğunda UartTxTask Acquire beklemesinden devam eder. **Bitiş damgası birinci zamanda alınır, ring kaydının kapanması ikinci zamanda yapılır.**

## Callback'in parametresi ve yaptığı iki iş

:::source Src/app_hal_callbacks.c 17 27

HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart), olayı bildiren UART nesnesinin adresini alır. `huart->Instance` o nesnenin hangi UART donanımına ait olduğunu belirtir. APP_UART_INSTANCE USART1'dir. Başka UART'tan bildirim gelmişse return yapılır; bu deneyin zaman damgası ve semaphore'u etkilenmez.

Bu gövde uygulamanın kendisine aittir. HAL'in weak varsayılan callback'i yerine link aşamasında kullanılır. Ana döngüde bu fonksiyonu elle çağıran bir satır görmemeniz normaldir: ilgili donanım olayını işleyen yol bu callback'e gelir. Normal TX DMA kullanımında UART tamamlanma callback'i son bitin tamamlanmasına ilişkin yoldadır; yalnızca DMA'nın bellekteki son baytı okumasıyla aynı an olarak düşünülmez.

İlk iş `g_tx_done_cycles = Timestamp_Now();` ile t4'ü paylaşılmış değişkene yazmaktır. İkinci iş `osSemaphoreRelease(TxDoneSemHandle)` ile bildirim bırakmaktır. Release tek parametre olarak hangi semaphore'a bildirim verileceğini alır; dönüş durumunu bu kod `(void)` ile kullanmaz.

**Semaphore bırakmak**, nesneyi silmek ya da belleğini free etmek değildir. En fazla bir bildirim tutan nesnede tamamlanma jetonunu sunmak ve onu bekleyen görevin bekleme koşulunu karşılamaktır. UartTxTask varsa beklemeden çıkıp Ready hâle gelebilir; ISR'ın ortasında sihirli biçimde kendi satırlarını yürütmez. Kesme çıkışı ve görev öncelikleri sonraki yürütmeyi belirler.

## Neden önce zaman, sonra Release?

Görev Acquire'dan başarıyla dönünce g_tx_done_cycles okuyacak. Bu nedenle sayı, uyanma bildirimi gönderilmeden önce hazır olmalıdır. Callback önce bildirip sonra yazsaydı tüketicinin ne okuyacağı sorusu ortaya çıkardı. Kodun sırası açık: **sonucu yaz -> sonucu bekleyeni haberdar et**.

Örnekte callback 648540000 cycle anında çalışsın: t0'dan 2500 us sonra. UartTxTask'ın CPU'yu yeniden aldığı an daha geç, örneğin t0+4000 us olabilir. Görev yeni Timestamp_Now çağırıp o geç zamanı t4 diye kullanmaz. `*t4 = g_tx_done_cycles` satırıyla callback'in aldığı 2500 us'a karşılık gelen cycle değerini kullanır. Böylece gönderim sonrası görev beklemesi R'yi 4000 us gibi göstermemiş olur. Callback'in kendi kesme işleme gecikmesi ise ölçüm sınırının içinde kalabilir.

## DMA hatası aynı uyandırmayı nasıl kullanır?

:::source Src/app_hal_callbacks.c 29 45

Hata callback'i de UART nesnesinin adresini alır. huart->ErrorCode hata bitlerini içerir. HAL_UART_ERROR_DMA biti varsa g_tx_dma_error=true yapılır, sonra semaphore bırakılır. Böylece görev tüm timeout süresini beklemeden hatayı görebilir. Acquire'ın osOK dönmesi bu nedenle "aktarımı kesin başarıyla bitirdik" anlamına gelmez; uart_tx_send bayrağı ayrıca okur.

RX tarafındaki ORE/FE/NE/PE bitleri için CmdRx_OnError çağrılır; yarım satır atılır ve alım yeniden kurulur. Bu RX dalı tek başına TX semaphore'unu bırakmaz. Aynı hata kodu her iki grubu içeriyorsa ayrı if blokları nedeniyle iki yol da çalışabilir.

## send_item t3 ve sonucu ring'e bağlıyor

:::source Src/app_tasks.c 61 76

ring_index==RING_INDEX_NONE ise öğe bir buton ölçüm kaydına bağlı değildir. Gönderim başarısızsa telemetri gönderim hatası sayacı artırılır; fonksiyondan çıkılır. BTN7 için ring_index=6 olduğundan bu erken çıkış uygulanmaz.

Önce RingBuffer_SetT3(6,7,t3) çalışır. Parametreler sırayla slot indeksi, beklenen olay kimliği ve daha önce alınmış t3'tür. Fonksiyon kritik bölgede slot_for ile doğrular, r->t3 yazar ve T3 maskesini set eder. Burada yeni zaman okuması yapılmaz. Ardından switch, uart_tx_send sonucuna göre kapanış yolunu seçer.

| uart_tx_send sonucu | Çağrılan fonksiyon | Parametreler / sonuç |
| --- | --- | --- |
| TX_OK | RingBuffer_CloseSuccess | (ring_index,event_id,t4): t4 ve OK/LATE sonucu yazılır. |
| TX_TIMEOUT | RingBuffer_CloseTimeout | (ring_index,event_id): TIMEOUT; timeout sayacı artar. |
| TX_START_ERR veya TX_DMA_ERR | RingBuffer_CloseTxErr | (ring_index,event_id): TX_ERR; ilgili hata sayacı artar. |

## OK/LATE kararını veren gerçek kod

:::source Src/app_ring_buffer.c 90 107

RingBuffer_CloseSuccess(uint16_t ring_index,uint16_t event_id,uint32_t t4) üç değeri alır, void döner. Kritik bölgeye girer; slot_for(6,7) hâlâ aynı olayı işaret ediyorsa t4'ü yazar ve T4 bitini ekler. "Aynı ring kaydı" ifadesinin kod karşılığı, işte bu **indeks + kimlik doğrulaması**dır.

`Timestamp_DeltaToUs(r->t0,t4)` örnekte (648540000-648000000)/216 = 2500 us verir. APP_DEADLINE_US 20000 olduğu için karşılaştırma doğru çıkar ve REC_STATUS_OK atanır. Dönüştürülmüş değer 20000'den büyükse REC_STATUS_LATE atanır, late_count artar. 20000 us dahil OK'dir. Mikrosaniyeye dönüşüm tam sayı olduğu için karar bu çözünürlüktedir.

success_count her iki durumda da artar: burada "success", zamanında olmayı değil aktarımın tamamlanmasını sayar. **LATE, gönderilemedi demek değildir.** DROP queue tesliminde kayıp; TX_ERR aktarım hatası; TIMEOUT tamamlanma bekleme aşımıdır. 20 ms deadline kendi başına DMA'yı durduran bir zamanlayıcı değildir; tamamlanan olayın sonucunu sınıflar.

Kayıt kapanınca t0..t4 slotta saklanır, ts_mask=15 yani 0b1111 olur. CPU'da çalışan değişkenlerin ömründen bağımsız olarak bu veri DUMP'a kadar ring içinde kalır; daha sonra yeni olaylar tamponu dolaşırsa üstüne yazılabilir. Güç kesilince RAM kaydı korunmaz.

:::snapshot 6

# 7 | STOP'u uygula, TX yolunu boşalt ve DUMP'ı gönder

Bu adımın ilk önemli ayrımı şudur: **STOP üretimi kapatan komuttur; DUMP kayıtları isteyen ayrı komuttur.** Kaynakta STOP işlendiğinde kendiliğinden dump_send_all çağrılmıyor. PC'den DUMP satırının da alınması, doğrulanması ve koşulunun sağlanması gerekir.

## STOP PC'den control_handle_command'a nasıl ulaşıyor?

PC `STOP,66\n` biçiminde bir satır gönderir. "66", STOP içeriğinin CRC-8 değerinin iki büyük harfli hex gösterimidir. RX callback, doğru UART için CmdRx_HandleByteReceived çağırır. Bu fonksiyon baytları static s_line içine toplar; LF gelince satır tamamlanır. Burada "STOP" kelimesi henüz uygulanmaz.

:::source Src/app_cmd_rx.c 24 51

Mevcut tek FIFO dalındaki Put, `osMessageQueuePut(CmdQueueHandle,&s_line,0u,0u)` çağrısıdır. Hedef CmdQueue, kaynak tamamlanmış satırın adresi, mesaj önceliği 0, bekleme 0'dır. Kuyruk oluşturulurken sizeof(CmdLine_t) verilmiş olduğundan len ve text[64] birlikte kopyalanır. ISR kendi s_line.len alanını sıfırlayıp yeni satır için kullanabilir; kuyruktaki kopya bağımsızdır.

CR biriktirilmez. Satır çok uzunsa discarding açılır ve sonraki LF'ye kadar atılır. Kuyruk doluysa bu Put başarısız olabilir; bu dosyada komut düşüşü için ayrı sayaç yoktur. Alımın sonunda rearm, bir sonraki bayt için Receive_IT isteğini kurar. Bunlar komutu taşıma işidir; deney durumunu değiştirme işi UartTxTask'tadır.

:::source Src/app_tasks.c 187 191

`CmdLine_t line`, UartTxTask'ın yerel alım nesnesidir. Get'in parametreleri CmdQueue tanıtıcısı, bu nesnenin adresi, öncelik çıktısı için NULL ve beklemeyen timeout=0'dır. Elde komut olduğu sürece while devam eder ve handle_cmd_line(&line) çağrılır. RX kesmesinin çalışmış olması, düşük öncelikli bu görevin komutu aynı anda işlediği anlamına gelmez.

:::source Src/app_tasks.c 164 173

handle_cmd_line(const CmdLine_t *line), ham satır nesnesinin adresini alır. `Protocol_ParseCommandLine(line->text,line->len,&cmd)` çağrısında birinci parametre LF hariç bayt dizisi, ikinci parametre geçerli uzunluk, üçüncü parametre çözümlenmiş komutun yazılacağı yerel Command_t adresidir. ParseResult_t sonucu döner.

Protocol_ParseCommandLine yerel kopyaya NUL ekler; son virgüldeki iki hex CRC karakterini çözer; içerik CRC'sini doğrular; alan sayısını ve komut adını denetler. START/STOP/DUMP/RESET_STATS argümansız komutlardır; CFG üç sayısal argüman taşır. STOP doğrulanırsa cmd.type=CMD_STOP yazılır. CRC uyuşmazlığında RingBuffer_IncCmdCrcErr; geçerli komutta control_handle_command; biçim hatasında uygulamadan çıkış yolu izlenir. Bozuk komut g_running'i değiştirmez.

control_handle_command(const Command_t *cmd), bu çözümlenmiş komutun adresini alır. cmd->type hangi switch dalının çalışacağını belirler. const, komut nesnesini bu fonksiyon üzerinden değiştirmediğini gösterir; fonksiyon buna rağmen global deney durumunu değiştirebilir. Değer döndürmez.

## STOP dalındaki iki satır üretimi nasıl kapatıyor?

:::source Src/app_tasks.c 129 137

`osEventFlagsClear(RunFlagsHandle,RUN_BIT)` iki parametre alır: hangi event flag nesnesi ve hangi bit maskesi temizlenecek? Ardından g_running=false yazılır. RUN_BIT, dış döngüsünde bu biti bekleyen TelemetryTask'ın çalışma iznidir. g_running ise buton kabulü ve telemetri iç döngüsünün okuduğu bool durumdur.

Bit önce temizlenir, bool sonra kapatılır. Kaynak yorumunun koruduğu durum, yüksek öncelikli görevin bool kapanmışken hâlâ açık duran RUN_BIT üzerinden dış beklemeye tekrar tekrar girmesidir. TelemetryTask ayrıca g_running false gördüğünde osDelay(1) yaparak geçişte CPU'yu bırakır. İşlemler tek bir C ataması değildir; kod bunları bilinçli sıraya koyar.

## g_running değişince hangi satırlar bunu görüyor?

:::source Src/app_tasks.c 266 290

Buradaki osEventFlagsWait çağrısının dört parametresi sırasıyla RunFlags nesnesi, beklenecek RUN_BIT maskesi, bekleme seçenekleri ve osWaitForever süresidir. osFlagsWaitAny, maskede istenen bitlerden birinin yeterli olmasını söyler; burada zaten bir bit var. osFlagsNoClear, bekleme karşılanınca RUN_BIT'i otomatik tüketmemeyi sağlar. Böylece bit deney boyunca açık kalır ve onu STOP açıkça temizler. g_running false gördüğü geçişte osDelay(1), bir tick bloklanma isteğidir; osKernelGetTickCount() ise parametresiz olarak mevcut kernel tick sayısını döndürür.

| Okuma noktası | STOP sonrası davranış |
| --- | --- |
| Button_HandleEXTI içindeki !g_running | Yeni kenar kabul edilmez; ring slotu ve ButtonQueue öğesi oluşturulmaz. GPIO kesmesi fiziksel olarak kapatılmış değildir. |
| TelemetryTask'ın while(g_running) koşulu | Sonraki tur başlatılmaz; dış beklemeye dönülür. |
| osDelayUntil sonrası if(!g_running) | Görev uyurken STOP geldiyse yeni yük/TEL üretmeden çıkılır. |
| Dış osEventFlagsWait | RUN_BIT temizlendiği için sonraki START'a kadar bloklu beklenir. |
| ButtonTask döngüsü | g_running kontrolü yoktur; daha önce kabul edilmiş olaylar işlenebilir. |
| UartTxTask döngüsü | Görev çalışmaya devam eder; bekleyen TX mesajlarını ve komutları işler. |

STOP tüm görevleri silmez, UART'ı kapatmaz, RX komut alımını kesmez, RTOS queue'larını boşaltmaz ve etkin DMA'yı iptal etmez. Önceden kabul edilmiş işler tamamlanabilir. TelemetryTask bir Put içinde zaten bloklanmışsa, o çağrı daha sonra sonuçlanabilir; g_running değişkeni geçmiş bir çağrıyı geriye dönük olarak iptal etmez. Bu nedenle **üretimin kapanması ile bütün işlerin tamamlanması aynı an değildir**.

STOP satırı RX'te alınırken UartTxTask hâlâ semaphore bekliyorsa, kontrol dalına ancak bu gönderim yordamından ve send_item'dan döndükten sonra ulaşır. Düşük görev önceliği nedeniyle ayrıca CPU beklemesi de olabilir. PC'nin gönderdiği an, firmware'in uyguladığı an ve son mesajın bittiği an ayrı zamanlardır.

## DUMP'ın koşulu nerede yazıyor?

:::source Src/app_tasks.c 139 150

Mevcut tek FIFO dalı `!g_running && osMessageQueueGetCount(TxQueueHandle) == 0u` koşulunu ister. Birinci koşul deneyin kapalı olmasını; ikinci koşul ortak TX kuyruğunda bekleyen öğe olmamasını ister. `&&` iki koşulun birlikte sağlanmasını gerektirir.

osMessageQueueGetCount tek parametre olarak kuyruk tanıtıcısını alır; kuyrukta bekleyen öğe sayısını döndürür. Daha önce Get ile alınmış, görevde işlenen öğeyi saymaz. Bununla birlikte bu uygulamada DUMP aynı UartTxTask ana döngüsünde işlenir; aynı görevin önceki send_item/uart_tx_send çağrısı bitmeden kontrol fonksiyonuna gelemez. Bunlar birlikte okunması gereken iki ayrı gerçektir.

Kodda ButtonQueue'nun boşluğunu denetleyen ek koşul yoktur. DUMP testi "sistemdeki her görev ve kuyruk kesinlikle boş" diyen genel bir tarama değildir; yalnızca yazılı bu şartları denetler. DUAL_QUEUE derleme dalında ise iki TX kuyruğunun da boş olması istenir. Bu belge mevcut tek FIFO üzerinden devam ediyor.

**Koşul sağlanmıyorsa DUMP beklemeye alınmaz.** O komutun işlenmesi biter; sonradan otomatik dump başlatacak bir bayrak set edilmez. PC'nin yeniden DUMP göndermesi gerekir. STOP ve hemen arkasından gelen DUMP aynı CmdQueue boşaltma döngüsünde işlenebilir; TX kuyruğu o anda doluysa DUMP reddedilmiş olur. Kaynakta yazılı davranış budur.

## dump_send_all'ın tamamı

:::source Src/app_tasks.c 90 105

dump_send_all(void) parametre almaz, void döner. Ring buffer'dan kayıt okur, g_cfg'den senaryo etiketini alır ve gönderim yardımcısını kullanır. Üç yerel nesneyle başlar: cursor dolaşma konumu, rec tek kayıt kopyası, frame üretilecek 64 baytın tamponudur.

Akış `REC × mevcut kayıt sayısı -> STAT -> DUMP_END` şeklindedir. Örneğin yedi kayıt varsa yedi ayrı 64 baytlık REC çerçevesi üretilir. Biz aşağıda yalnızca olay 7'nin içeriğini örnekliyoruz; diğer olayların varlığını silmiyoruz. Hiç kayıt yoksa while çalışmaz ama STAT ve DUMP_END yine gönderilir.

## RingBuffer_DumpNext bir kaydı nasıl alır?

:::source Src/app_ring_buffer.c 167 181

| Parametre / sonuç | Çağrıdaki ifade | Anlamı |
| --- | --- | --- |
| uint32_t *cursor | &cursor | Çağıranın dolaşma sayacının adresi. Fonksiyon bir kayıt alınca bunu artırır. İlk değer 0. |
| EventRecord_t *out | &rec | Alınan kayıt kopyasının yazılacağı adres. |
| Dönüş: bool | while koşulu | Bu adımda geçerli kayıt bulundu mu? false olduğunda döngü biter. |

wrapped, toplam ayrılan kayıtların kapasiteyi aşıp aşmadığını söyler. count, en çok 256 olacak biçimde okunacak kayıt sayısıdır. Tampon henüz sarmadıysa başlangıç 0; sardıysa en eski mevcut slot s_total_allocated % 256'dır.

`*out = s_records[(start + *cursor) % 256]` satırı **yapı atamasıyla kayıt kopyalar**. memcpy çağrısı yazılmamış olsa da EventRecord_t alanlarının değeri rec nesnesine aktarılır. Ring içindeki kaydın adresi dışarı döndürülmez. Kritik bölge dışında, bu kopyadan metin oluşturulabilir. Böylece snprintf ve UART bekleme boyunca ring koruması tutulmaz.

`(*cursor)++`, işaretçinin gösterdiği sayacı artırır; işaretçinin kendisini bir sonraki adrese taşımak değildir. 300 olay ayrılmışsa 44 en eski kayıt üzerine yazılmıştır; dolaşım mevcut 45..300 olaylarını eski-yeni sırasıyla verir. DUMP kayıtları ring'den silmez; yeni bir DUMP baştan yine okuyabilir.

## Protocol_BuildREC neyi dönüştürür?

:::source Src/app_protocol.c 111 124

İki parametresi vardır: out/frame, 64 baytlık hedef; const EventRecord_t *rec, dönüştürülecek kayıt kopyasının adresi. Sonuç bool, çerçevenin normal içeriğiyle sığıp sığmadığını bildirir. dump_send_all bu dönüşü `(void)` ile kullanmaz; frame içeriğini gönderir. Sığma denetimi ve ERR yolu, BTN'de anlattığımız finish_frame ile aynıdır.

fmt_delta her t1..t4 için çağrılır. Parametreleri yazılacak geçici buf adresi, kayıt adresi, kontrol edilecek REC_TS bit maskesi ve dönüştürülecek ham t değeridir. Maske set değilse "-" döndürür; set ise Timestamp_DeltaToUs(rec->t0,t) sonucunu metne çevirir. Döndürdüğü const char * bu metnin adresidir. status_to_str(rec->status) da sayısal durumu OK/LATE/DROP/TX_ERR/TIMEOUT/PENDING metnine çevirir.

:::source Src/app_protocol.c 64 71

Dört ayrı geçici dizi b1,b2,b3,b4 bulunmasının nedeni dört sayı metninin aynı anda snprintf argümanı olmasıdır. Tek bir ortak tamponun son içeriğine dönüştürülmezler. Her biri kendi metnini tutar; alanlar birbirini ezmez.

Olay 7 için içerik `REC,7,2,1500,40,120,1800,2500,OK` olur. **40,120,1800,2500 değerleri sırasıyla t1-t0, t2-t0, t3-t0,t4-t0'dır.** Bunları dört ayrı aşamanın süresi diye toplamıyoruz. Aşamalar 40; 120-40=80; 1800-120=1680; 2500-1800=700 us olur. R=2500 us'tur. Tamamı öğretici sayılardır; t2-t4 bilgisi canlı BTN'de olmayıp burada bulunur.

## STAT sayaçları hangi sırayla toplar?

`RingBuffer_GetCounters(void)` parametresizdir, RingCounters_t yapı kopyası döndürür. Kritik bölge içinde `copy = s_counters` yapar; çıkar, copy'yi döndürür. Bu kopyanın adresi Protocol_BuildSTAT(frame,g_cfg.scenario_id,&counters) çağrısına verilir. İlk parametre hedef, ikinci parametre deney etiketi, üçüncü parametre sayaçların salt okunur adresidir. Sonuç bool'dur.

:::source Src/app_protocol.c 126 143

| STAT'taki alan sırası | Kodda kullanılan değer | Ne sayıyor? |
| --- | --- | --- |
| 1: scenario | scenario_id | Hangi deney etiketinin özeti? |
| 2: success | success_count | Tamamlanan BTN: OK + LATE. |
| 3: drop | btn_queue_drop_count + tx_queue_drop_count | Buton olayı iki queue tesliminden birinde kaybedildi. |
| 4: tx_err | tx_start_err_count | BTN gönderim başlatma/DMA hatası. |
| 5: timeout | tx_timeout_count | BTN için TC bildirim bekleme aşımı. |
| 6: ring_ovf | ring_overflow_count | Üzerine yazılmış eski olay kaydı. |
| 7: cmd_crc_err | cmd_crc_err_count | PC komutunda CRC uyuşmazlığı. |
| 8: tel_drop | tel_drop_count | TEL, TX kuyruğuna konamadı. |
| 9: tel_tx_fail | tel_tx_fail_count | TEL'in aktarımı başarısız oldu. |
| 10: id_mismatch | id_mismatch_count | Slot/olay kimliği doğrulaması başarısız oldu. |

late_count ring sayaç yapısında tutulur ama bu STAT biçiminde ayrı alan olarak yazılmaz. LATE olan olaylar REC durum alanından görülebilir. Ring taşmışsa elde kalan REC sayısıyla tüm deney boyunca artmış success_count aynı olmak zorunda değildir; biri saklanan kayıtlar, diğeri sayaçtır.

## DUMP_END ve gönderimin gerçek yolu

:::source Src/app_protocol.c 145 149

Protocol_BuildDumpEnd yalnızca out/frame adresi alır. Sabit DUMP_END içeriğinin `sizeof(content)-1` uzunluğu, sondaki C NUL karakterini hariç tutar. finish_frame yine 62 bayta dolgu, bir CRC ve bir LF ile toplam 64 bayt üretir. Bu işaret, bilgisayara mevcut dump dizisinin son çerçevesi gönderildi bilgisini vermek içindir.

:::source Src/app_tasks.c 79 86

send_meta(const uint8_t frame[64]), çerçevenin adresini alır; void döner. Yerel t3 ve t4 adresleriyle aynı uart_tx_send'i çağırır. Başarısızsa bir kere daha çağırır: toplam en çok iki deneme. İkinci sonuç kullanılmaz. Bu yeniden deneme, ölçülen canlı BTN gönderiminin send_item yolunda yoktur.

**REC, STAT ve DUMP_END TxQueue'ya konmuyor.** Zaten UART'ın sahibi olan UartTxTask, dump_send_all -> send_meta -> uart_tx_send zinciriyle doğrudan sırayla gönderiyor. uart_tx_send tamamlanma/hata sonucunu beklediği için bir metadata çerçevesi sonuçlanmadan sonrakine geçilmiyor. Bu çerçevelerin callback t4'leri normal şekilde alınır ama send_meta bunları olay 7'nin ring kaydına yeniden yazmaz.

Dump boyunca RX kesmeleri yeni komut satırı toplayabilir; UartTxTask kendi dump_send_all çağrısından dönene kadar ana döngüde yeni komut uygulayamaz. DUMP kayıtları temizlemez. Yeni START ring kayıtlarını ve sayaçları yeniden sıfırladığında yeni deney başlar; queue'ları sıfırlayan bir START çağrısı bu kaynakta yoktur.

:::snapshot 7

## Bütün yolda hangi kopyalar yapıldı?

| Geçiş | Kopyayı yapan satır / çağrı | Kopyalanan şey |
| --- | --- | --- |
| ISR -> ButtonQueue | osMessageQueuePut(...,&evt,...) | ButtonEvent_t içeriği. |
| ButtonQueue -> ButtonTask | osMessageQueueGet(...,&evt,...) | Görevin yerel olay nesnesine aynı tipte kopya. |
| Geçici metin -> item.frame | pack_payload içindeki memcpy | BTN metninin len baytı; sonra dolgu/CRC/LF eklenir. |
| ButtonTask -> TxQueue | osMessageQueuePut(...,&item,...) | Tüm TxItem_t: kimlik bağlantısı + frame. |
| TxQueue -> UartTxTask | osMessageQueueGet(...,&item,...) | Göndericinin yerel TxItem_t nesnesi. |
| Alınan frame -> s_tx_buffer | memcpy(s_tx_buffer,frame,64) | DMA'nın okuyacağı tam 64 bayt. |
| ISR'ın t4'ü -> send_item yereli | *t4 = g_tx_done_cycles | Tek 32 bit sayı. Semaphore bu sayıyı taşımaz. |
| Yerel t1..t4 -> ring | RingBuffer_Set... ve CloseSuccess | Doğrulanmış slotun zaman alanları. |
| Ring -> dump'taki rec | *out = s_records[...] | Tek EventRecord_t kopyası. |
| Sayaçlar -> dump'taki counters | copy = s_counters | RingCounters_t kopyası. |

## Bu yedi adımın temel bağı

İlk kabulde üretilen **event_id=7 ve ring_index=6** aynı olayı bağlar. ButtonQueue bunları olayla taşır. TxQueue bunları hazır mesajla taşır. Gönderici, sonuç geldiğinde bu ikiliyle doğru kaydı bulur. DUMP ise tamamlanmış kayıt kopyasını metne çevirir. CPU sırası, queue sırası ve kayıt kimliği farklı görevler üstlenir; aralarındaki bağlantıyı bu alanlar ve çağrılar kurar.

Kaynak esasları: uygulama gövdeleri `firmware/Button_Task/Core/Src/app_*.c`, tipler `Core/Inc/app_*.h`. RTOS çağrılarının kullanılan sözleşmeleri için [Arm Message Queue](https://arm-software.github.io/CMSIS_6/main/RTOS2/group__CMSIS__RTOS__Message.html) ve [Arm Semaphore](https://arm-software.github.io/CMSIS_6/main/RTOS2/group__CMSIS__RTOS__SemaphoreMgmt.html) başvurularına bakılabilir. Bu belge kütüphanelerin iç implementasyonunu veya alternatif tasarım önerilerini anlatmaz; mevcut uygulamanın işlemlerini açıklar.
