/*
 * kirmizi_roi.cpp
 * ===============
 * AMAÇ
 * ----
 * Kayıtlı karelerdeki kırmızı ROI çerçevesini bulan fonksiyonun gerçeklemesi.
 * Kamera görüntüsü gri tonlu olduğu için tek renkli (kırmızı) pikseller sadece
 * sistemin çizdiği çerçeveye aittir; basit bir renk eşiği yeterlidir.
 *
 * İÇERİK
 * ------
 * - kirmizi_roi_bul(): Renk eşiği -> en büyük kontur -> sınır dikdörtgeni ->
 *                      çerçeve çizgisini dışarıda bırakmak için içe 4 px kırpma.
 */
#include "kirmizi_roi.h"

#include <opencv2/imgproc.hpp>

// OpenCV 5'te getRotationMatrix2D, contourArea ve boundingRect fonksiyonları
// imgproc'tan yeni "geometry" modülüne taşındı; 4.x'te bu başlık yok.
// Başlık dosyası varsa ekle (sürüm numarasından bağımsız, her iki sürümde çalışır)
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif

#include <vector>

bool kirmizi_roi_bul(const cv::Mat& bgr, cv::Rect& roi)
{
	// Renkli olmayan görüntüde kırmızı aranamaz
	if (bgr.empty() || bgr.channels() < 3)
		return false;

	// Kanalları ayır ve çıkarma işleminde taşma olmasın diye 16 bit işaretliye çevir
	std::vector<cv::Mat> kanal;
	cv::split(bgr, kanal);
	cv::Mat b, g, r;
	kanal[0].convertTo(b, CV_16S);
	kanal[1].convertTo(g, CV_16S);
	kanal[2].convertTo(r, CV_16S);

	// Kırmızı: R yüksek, G ve B ondan belirgin şekilde düşük
	cv::Mat maske = (r > 150) & ((r - g) > 80) & ((r - b) > 80);

	// Kırmızı bölgelerin dış konturlarını bul
	std::vector<std::vector<cv::Point>> konturlar;
	cv::findContours(maske, konturlar, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	if (konturlar.empty())
		return false;

	// En büyük kırmızı şekil = ROI çerçevesi
	size_t en_buyuk = 0;
	double en_alan = -1;
	for (size_t i = 0; i < konturlar.size(); ++i) {
		double a = cv::contourArea(konturlar[i]);
		if (a > en_alan) {
			en_alan = a;
			en_buyuk = i;
		}
	}
	cv::Rect r0 = cv::boundingRect(konturlar[en_buyuk]);

	// Çok küçükse (yazı, gürültü) çerçeve sayma
	if (r0.width < 40 || r0.height < 20)
		return false;

	// Çerçeve çizgisini dışarıda bırakmak için içe 4 piksel kırp
	const int ic = 4;
	roi = cv::Rect(r0.x + ic, r0.y + ic, std::max(1, r0.width - 2 * ic), std::max(1, r0.height - 2 * ic));
	return true;
}