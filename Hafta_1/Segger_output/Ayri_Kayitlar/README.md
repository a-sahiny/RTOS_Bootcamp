# Ayrı SystemView kayıtları

Orijinal üç dosya önceki oturumları da içeriyor: 5msLoad 1–6, NoLoad 1–9, 2msLoad 1–10. Önceki dosyanın olay akışı sonraki dosyanın başında bayt bayt aynı. Bu nedenle kayıtların başına bakınca aynı grafik görülüyor.

Bu klasörde her dosyanın yalnız son Trace Start → Trace Stop oturumu var:

| Dosya | Kaynak oturum | Süre | TelemetryTask medyan çalışma süresi |
| --- | --- | --- | --- |
| [10ms_NoLoad_tek_oturum.SVDat](10ms_NoLoad_tek_oturum.SVDat) | 9 | 10.940 s | 0.157 ms |
| [10ms_2msLoad_tek_oturum.SVDat](10ms_2msLoad_tek_oturum.SVDat) | 10 | 13.521 s | 2.158 ms |
| [10ms_5msLoad_tek_oturum.SVDat](10ms_5msLoad_tek_oturum.SVDat) | 6 | 12.079 s | 5.159 ms |

SystemView içinde File → Load Recording (Ctrl+O) ile bu dosyaları aç. Timeline ölçeğini üçünde de örneğin 20 ms yapıp TelemetryTask çalışma bloklarını karşılaştır. CPU Load karşılaştırmasında telemetri üretiminin sürdüğü bölümü seç; NoLoad kaydının sonunda üretimin durduğu bir bölüm de var.

Dosya adlarının son oturumları temsil ettiği kabul edildi. Ölçülen süreler bunu destekliyor; CFG komutu ve load_iter değeri bu trace içinde yok. Çalışma süresi, yapay yükün yanında çerçeve hazırlama ve kuyruk işlemlerini de kapsıyor.

Doğrulama: Olay baytları kaynak oturumla birebir aynı, tüm paketler yeniden çözüldü, olaylar ve zaman farkları karşılaştırıldı. Her çıktıda bir Start ve bir Stop var. Orijinal dosyaların SHA-256 değerleri değişmedi. Dosya başlığındaki kaynak kayıt zamanı korundu; Title/Description alanları ayrıştırmayı belirtiyor. Ayrıntılar verification.json içinde. SystemView arayüzünde açılış ayrıca doğrulanmadı.

Kayıtta bazı kesmeler gizli olduğu için trace üzerinden hesaplanan task/Idle payları kesin fiziksel CPU yükü olarak yorumlanmamalı.

SystemView kayıt açma belgesi: https://doc.segger.com/UM08027_SystemView.html#Save_and_load_recordings
