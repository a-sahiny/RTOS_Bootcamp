# Rapor — Yük Altında Buton Yanıt Süresi

> **Durum:** Ölçüm yapılmadı. Bu belge yapıyı ve beklentileri içerir; sonuç tabloları ve
> yorumlar kart üzerindeki S0–S5 koşumlarından sonra doldurulacak. Hiçbir değer tahminle
> yazılmamalı — kaynak her zaman `measurements/summary.csv`.

## 1. Yöntem

- R = t₄ − t₀, kart üzerinde DWT (216 MHz) ile; aşamalar S1 = t₁−t₀, S2 = t₂−t₁,
  S3 = t₃−t₂, S4 = t₄−t₃ ([SPEC.md §6](../SPEC.md)).
- Her senaryoda en az 30 buton basışı önerilir (istatistik için); basışlar arasında ≥ 0.5 s.
- Deadline: R ≤ 20 ms.

## 2. Beklentiler (ölçümle doğrulanacak hipotezler)

| Hipotez | Gerekçe |
|---|---|
| S4 tüm senaryolarda ≈ 5.56 ms | 64 B × 10 bit / 115200 bps |
| S0'da R ≈ S4 + birkaç µs, S3 ≈ 0 | Kuyruk neredeyse hep boş |
| Periyot kısaldıkça S3 büyür | BTN, FIFO'da önündeki TEL'lerin (her biri 5.56 ms) bitmesini bekler |
| CPU yükü arttıkça S1 büyür | TelemetryTask (yüksek öncelik) yük sırasında ButtonTask'ı bekletir |
| S2 senaryodan bağımsız, küçük (µs) | Yanıt üretimi sabit iş; yalnızca yük sırasında kesintiye uğrarsa büyür |
| S5'te LATE ve DROP görülür | Yük ≥ periyot, TX kuyruğu dolar |

## 3. Sonuçlar

| Senaryo | Periyot | Yük (iter / ölçülen µs) | Başarılı | R min / ort / maks (ms) | > 20 ms | Drop | TX hata | Timeout | Kayıt kaybı |
|---|---|---|---|---|---|---|---|---|---|
| S0 | | | | | | | | | |
| S1 | | | | | | | | | |
| S2 | | | | | | | | | |
| S3 | | | | | | | | | |
| S4 | | | | | | | | | |
| S5 | | | | | | | | | |

Aşama ortalamaları (µs):

| Senaryo | S1 | S2 | S3 | S4 |
|---|---|---|---|---|
| S0 | | | | |
| … | | | | |

## 4. Grafikler

- Olay # → R: `plots/S0_R.png` … `plots/S5_R.png`
- Senaryo → aşama süreleri: `plots/stages_by_scenario.png`

## 5. Yorum

_Ölçümlerden sonra: hangi aşama hangi parametreyle değişti, hipotezler tuttu mu,
beklenmeyen gözlemler._

## 6. Doğruluk kontrolü

Her senaryo için `summary.csv`'de `config_verified = 1`, `id_mismatch_count = 0`,
`pending_count = 0` olmalı; `record_loss_count` ile `ring_overflow_count` tutarlı olmalı.
