/*
 * rapor.h
 * =======
 * AMAÇ
 * ----
 * Test programının rapor modunun arayüzü (gerçeklemesi rapor.cpp'de).
 * Sadece TEST içindir; teslim edilecek barkod_oku.h/.cpp bu dosyaya bağımlı değildir.
 *
 * İÇERİK
 * ------
 * - rapor_calistir(): Klasörü ve alt klasörlerini tarar, her alt klasörü bir kutu
 *                     sayarak kutuların en az bir karede okunup okunmadığını raporlar.
 */
#pragma once

#include <string>

// klasor   : taranacak klasör; boşsa (Windows) klasör seçme penceresi açılır
// limit_ms : barkod_oku() süre sınırı (0 = sınırsız)
// tum      : true ise kırmızı ROI aranmaz, fotoğrafın tamamı okunur
// Dönüş    : 0 başarılı, 1 klasör bulunamadı / boş
int rapor_calistir(const std::string& klasor, double limit_ms, bool tum);