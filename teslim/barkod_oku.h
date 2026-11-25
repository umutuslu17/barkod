/*
 * barkod_oku.h
 * ============
 * AMAÇ
 * ----
 * Hareketli banttaki teneke kutuların barkod crop'unu zxing-cpp (v3.1.1) ile
 * okuyan fonksiyonun arayüzü. Python'daki barkod_oku.py'nin C++ karşılığıdır;
 * aynı ön işleme akışını ve oylama kuralını uygular.
 *
 * Neden ön işleme gerekiyor: Orijinal kamera görüntüsünde (4200x200 px) barkod
 * yeterince büyük ama piksel gürültüsü var. ZXing'in hiçbir sürümü (v1.4.0 -
 * v3.1.1) bu gürültülü crop'ları ham hâliyle okuyamıyor. Görüntü INTER_AREA ile
 * küçültülüp gürültü ortalanınca ve kademeli ön işleme uygulanınca okunuyor.
 *
 * İÇERİK
 * ------
 * - BarkodYon   : Barkodun görüntüdeki yönü (otomatik, yatay, dikey).
 * - BarkodAyar  : Fonksiyonun ayarları (süre sınırı, gereken oy sayısı, yön).
 * - BarkodSonuc : Okuma sonucu (barkod metni, kullanılan yöntemler, süre, deneme sayısı).
 * - barkod_oku(): Crop'u alıp barkodu okuyan ana fonksiyon.
 * - zxing_ham_oku(): Hiç ön işleme yapmadan, kütüphanenin varsayılan ayarlarıyla
 *                    tek okuma (karşılaştırma için).
 */
#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// Barkodun görüntüdeki yönü.
//   Yatay : çubuklar dikey, barkod soldan sağa okunur (eski veri setleri)
//   Dikey : çubuklar yatay, barkod yukarıdan aşağı okunur (kutu 90° dönük)
//   Otomatik: fonksiyon yönü görüntüden kendisi tahmin eder
enum class BarkodYon { Otomatik, Yatay, Dikey };

struct BarkodAyar
{
	// Bu süre (ms) aşılırsa kalan denemeler yapılmadan sonuç boş döner.
	// Sınır her denemeden sonra kontrol edildiği için gerçek süre en fazla
	// bir deneme kadar (~1-3 ms) aşabilir. 0 veya negatif verilirse sınır yok.
	// 10 ms: testlerde her kutu en az bir karede 2-6 ms'de okundu; okunamayan
	// karede de kamera döngüsünü uzun süre bekletmez.
	double zaman_limiti_ms = 10.0;

	// Bir sonucun kabul edilmesi için kaç FARKLI yöntemin aynı barkodu
	// okuması gerektiği. Agresif ön işleme (özellikle döndürme) nadiren kontrol
	// hanesi tutan ama yanlış bir barkod üretebiliyor; 2 oy bunu eliyor.
	int gereken_oy = 2;

	// Barkodun yönü. Hattaki kamera/kutu düzeni biliniyorsa Yatay veya Dikey
	// verilmesi en güvenlisidir; Otomatik'te yön, kenar yönlerine bakılarak
	// (~0.2 ms) tahmin edilir.
	BarkodYon yon = BarkodYon::Otomatik;
};

struct BarkodSonuc
{
	std::string metin;                 // Okunan barkod; okunamadıysa boş
	std::vector<std::string> yontemler; // Aynı sonucu veren yöntemlerin adları
	double sure_ms = 0.0;              // Fonksiyonun toplam çalışma süresi
	int deneme = 0;                    // ZXing'e kaç kez okuma yaptırıldığı

	bool okundu() const { return !metin.empty(); }
};

// Crop edilmiş barkod bölgesini (BGR, BGRA veya gri) okur.
BarkodSonuc barkod_oku(const cv::Mat& crop, const BarkodAyar& ayar = {});

// Karşılaştırma için: ön işleme yok, ZXing'in varsayılan ayarlarıyla tek okuma.
BarkodSonuc zxing_ham_oku(const cv::Mat& crop);