# Erhan Hocaya Notlar

Hocam, ilk denememde S5 senaryosunda (100 Hz, 5 ms yük) veri kayıpları yaşandığını gördüm. İlk olarak buffer sayısının yetersiz olabileceğini düşündüm ve buffer sayısını artırdım.

İlgili ekran görüntüleri: [Buffer_Az](analysis/plots/ekran/Buffer_Az)

Buffer sayısını artırdıktan sonra veri kaybının ortadan kalktığını, ancak sistemin zamanlama açısından hâlâ çok geride kaldığını gördüm. 20 ms'lik deadline ciddi ölçüde aşılıyordu. Bunun üzerine AI ile birlikte ikili buffer sistemi kurduk ve bu yapıyla tekrar denedim. Zaman aşımları azalmıştı, ancak hâlâ devam ediyordu. Analizleri incelediğimde TX öncesi kuyrukta çok fazla veri biriktiğini fark ettim ve kullandığım baud hızının yetersiz olduğunu anladım.

İlgili ekran görüntüleri: [Buffer_Artmis](analysis/plots/ekran/Buffer_Artmis)

Bunun üzerine baud hızını artırdım ve sorunun çözüldüğünü gördüm.

İlgili ekran görüntüleri: [Baud_Hizli](analysis/plots/ekran/Baud_Hizli)

Bazen overengineering yapmamak gerekiyormuş. :)
