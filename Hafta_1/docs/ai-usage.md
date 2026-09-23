# Yapay zekâ kullanımı

Bu haftanın tasarım ve kodunun büyük kısmı bir yapay zekâ asistanıyla (Claude, Claude Code
üzerinden) birlikte hazırlandı. Bu belge neyin kimden geldiğini ve nasıl doğrulandığını kayıt altına alır.

## 1. Kararlar (öğrenciye ait)

- Deney tanımı, görev/öncelik tablosu, UART standardı, t₀–t₄ tanımları, ölçüm doğruluğu kuralları,
  arayüz gereksinimleri, klasör yapısı.
- Platform: STM32F746G-DISCO C01, STM32CubeIDE, CMSIS-RTOS v2, Python + PyQt6.
- IWDG kullanılmaması, 1 bayt CRC-8 eklenmesi, 30 ms debounce.
- Önce spec, sonra kod; ardından tüm çalışmanın gereksinimlere karşı denetlenmesi.
- Çakışmalarda CubeIDE/CubeMX'in ürettiği kod korunur; uygulama koduna uyarlanır.

## 2. Yapay zekânın ürettikleri

- `SPEC.md` (v0.1 → v0.3), firmware uygulama katmanı (`Cubeide_Manuel/Core/*/app_*`), `UART_RTOS_ODEV1.ioc` üzerindeki değişiklikler, PC arayüzü, dokümanlar.
- Proje dosyasını (`UART_RTOS_ODEV1`) öğrenci CubeIDE'de oluşturdu; yapay zekâ öğrencinin kararlarıyla (USART1, HSE, `.ioc`'u yapay zekâ düzenlesin) bu `.ioc`'u değiştirdi ve `main.c` USER CODE bloklarını doldurdu. Önceki bağımsız `ButtonLatency.ioc` taslağı kaldırıldı.
- Önerdiği ve öğrencinin kabul ettiği tasarımlar: deney sonu dump (ölçüme karışmamak için),
  RX ISR + CmdQueue ile 4. görev eklemeden komut kanalı, 64 baytlık çerçevede CRC konumu,
  115200 baud bant genişliği hesabı ve 10 ms minimum periyot.

## 3. Doğrulama

- **Kart bilgileri** yapay zekânın hafızasından değil kaynaklardan alındı: Zephyr
  `stm32f746g_disco.dts` (HSE 25 MHz kristal, PLL 25/432/2/9, USART1 PA9/PB7, PI11 aktif-yüksek),
  NuttX kart dokümanı, UM1907. Cortex-M7 DWT `LAR` gereksinimi ST topluluk kaynaklarıyla
  doğrulandı. Araç sürümleri (CubeMX 6.17.0, CubeIDE 2.2.0, FW_F7 1.17.4) ve CubeIDE 2.x'te
  CubeMX'in ayrı araç olduğu ST duyurularından doğrulandı.
- **Kod** host üzerinde derlenip otomatik testlerle doğrulandı (ayrıntı: `code-notes.md` §4).
- **Denetim turu (v0.3):** öğrencinin isteğiyle tüm çalışma gereksinimlere karşı satır satır
  tarandı; önceki sürümlerde yapay zekânın yaptığı ve düzeltilen hatalar:
  - t₄ görevde ölçülüyordu (S4 şişerdi); S2 hep ~0 ölçülüyordu; deadline ms'ye kesiliyordu.
  - DWT `LAR` kilidi açılmıyordu (kart debugger'sız çalışınca tüm süreler 0 olurdu).
  - `.ioc`'ta `CoupleFile=false` idi (`usart.h` üretilmez, derleme kırılırdı); TIM6 hem
    timebase hem ayrı zamanlayıcı olarak tanımlıydı; EXTI yalnız yükselen kenardaydı.
  - `snprintf` kesmesi nedeniyle uzun mesaj koruması hiç çalışmıyordu (sessiz kesme).
  - TelemetryTask START'ta TEL patlaması üretiyor, aşırı yükte tüm alt görevleri aç bırakabiliyordu.
  - UART RX hatası komut kanalını kalıcı olarak susturabiliyordu.
  - SPEC'teki komut CRC örnekleri uydurmaydı; gerçek değerlerle değiştirildi.
  - Firmware README, CubeMX'in artık CubeIDE içinde olmadığı en güncel araç akışına uymuyordu.
  - Arayüzde: DROP olayları R=0 görünüyordu, CSV ayarları kartınkinden farklı olabiliyordu,
    DUMP yanıtsız kalırsa arayüz sonsuza kadar bekliyordu, `summary.csv`'de tekrarlı satır oluşuyordu.

- **Gerçek ARM derlemesi:** kurulu CubeIDE 1.16.0 derleyicisi ve FW_F7 1.17.4 kaynaklarıyla yapıldı (`code-notes.md` §4). Bu sırada iki gerçek sorun daha bulundu: hook'ların `cmsis_os2.c`'deki weak tanımlarla yarışması ve USART1 `OverSampling` değerinin F7 için yanlış olması.
- **Hata (yapay zekâ):** hook'ları `app_hal_callbacks.c`'de güçlü tanım yaptı; bunu öğrencinin CMSIS_V1 projesinde CubeMX'in hook'ları `__weak` ürettiğini görerek varsaydı. CMSIS_V2'de CubeMX güçlü tanım üretiyor; öğrencinin ilk derlemesi "multiple definition" ile düştü. İlk düzeltme denemesi CubeMX'in hook gövdelerini ve `defaultTask` döngüsünü değiştiriyordu; öğrenci çakışmalarda CubeIDE kodunun korunmasını istedi. Son hâl: uygulama hook tanımlamaz, `freertos.c` ve `StartDefaultTask` CubeMX çıktısıyla aynı; yalnızca boş USER CODE bloklarına ekleme var.
- **Olay:** `.ioc` doğrulaması için CubeMX komut satırı modunda çalıştırıldı; bu mod onay pencerelerini öğrencinin masaüstünde açtı. Öğrenci pencereye "No" dedi, hiçbir şey üretilmedi (deneme scratch kopyadaydı). Sonrasında CubeMX bir daha çalıştırılmadı.

## 4. Sınırlar

- Kod üretimi CubeMX'te öğrenci tarafından yapılacak; karta erişim yoktu, kart üzerinde çalışma doğrulanmadı.
- Ölçüm verisi ve rapor sonuçları **üretilmedi**; `measurements/` ve `analysis/report.md`
  sonuçları gerçek kart ölçümleriyle doldurulacak.
