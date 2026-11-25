/*
 * main.cpp
 * ========
 * AMAÇ
 * ----
 * Tek bir fotoğraftaki barkodu zxing-cpp v3.1.1 ile okuyup süresini ölçen
 * test programı. Her okuma için iki sonuç verir:
 *   1) Sadece ZXing  : ön işleme yok, kütüphanenin varsayılan ayarları
 *                      (= "kütüphanenin son sürümü tek başına yeterli mi?")
 *   2) barkod_oku()  : ön işleme + oylama akışı
 * ve okumanın 10 ms hedefinin altında kalıp kalmadığını yazar.
 *
 * KULLANIM
 * --------
 *   barkod_oku_cpp                 -> (Windows) dosya seçme penceresi açılır,
 *                                     sonuç pencerede gösterilir
 *   barkod_oku_cpp foto.jpg [...]  -> verilen fotoğrafları okur
 *   barkod_oku_cpp --rapor [klasör]  -> RAPOR MODU: klasörü ve tüm alt klasörlerini
 *                                     tarar; her alt klasörü bir kutu sayar ve her kutunun
 *                                     en az bir karesinde barkodun okunup okunmadığını
 *                                     raporlar. Klasör verilmezse (Windows) seçme penceresi açılır.
 *                                     (Rapor kodu ayrı dosyada: test/rapor.h, test/rapor.cpp)
 *
 * PENCERE TUŞLARI (sonuç penceresi açıkken)
 *   R        : bölgeyi fareyle seç (ENTER/SPACE onay, C iptal), sadece o bölgeyi oku
 *   A        : otomatik bölgeye (kırmızı ROI) geri dön
 *   O        : başka bir fotoğraf aç
 *   N        : sonraki fotoğraf (birden fazla fotoğraf verildiyse)
 *   Q / ESC  : çıkış
 *   (diğer tuşlar yok sayılır; böylece seçimi onaylarken basılan ENTER/SPACE
 *    yanlışlıkla programı kapatmaz)
 *
 * KOMUT SATIRI SEÇENEKLERİ
 *   --roi x,y,w,h   bölgeyi elle ver (piksel)
 *   --sec           açılışta bölgeyi fareyle seç
 *   --tum           fotoğrafın tamamını oku
 *   --limit ms      barkod_oku() süre sınırı (varsayılan 10, 0 = sınırsız)
 *   --tekrar N      süre ölçümü için kaç kez çalıştırılacağı (varsayılan 20)
 *   --goster        sonucu pencerede göster (dosya penceresiyle açılınca otomatik)
 *   --rapor [klasör] klasör taraması (yukarıya bakın); --limit ve --tum ile birlikte kullanılabilir
 *
 * İÇERİK
 * ------
 * - Secenekler          : Komut satırı seçenekleri.
 * - Olcum               : Bir bölgenin okuma sonucu ve süreleri.
 * - argumanlari_oku()   : Komut satırını Secenekler'e çevirir.
 * - dosya_sec()         : (Windows) dosya seçme penceresini açar.
 * - resim_yukle()       : Türkçe karakterli yollarda da çalışan resim okuma.
 * - medyan()            : Süre ölçümlerinin ortancasını hesaplar.
 * - yontem_yaz()        : Oy veren yöntemleri " | " ile birleştirir.
 * - otomatik_bolge()    : Kırmızı ROI'yi bulur, yoksa tüm fotoğrafı döner.
 * - olc()               : Verilen bölgeyi iki yöntemle okur, süreleri ölçer, terminale yazar.
 * - ekran_ciz()         : Fotoğraf + bölge çerçevesi + sonuç şeridini çizer.
 * - goruntule()         : Sonuç penceresini gösterir, R/A/O/Q tuşlarını işler.
 * - fotograf_isle()     : Bir fotoğrafı yükler, bölgeyi belirler, okur, gösterir.
 * - main()              : Girişi hazırlar ve her fotoğraf için fotograf_isle()'yi çağırır.
 */
#include "barkod_oku.h"
#include "kirmizi_roi.h"
#include "rapor.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

const char* PENCERE = "Barkod Test";

struct Secenekler
{
	std::vector<std::string> dosyalar;
	bool elle_roi = false;  // --roi verildi mi
	cv::Rect roi;           // --roi değeri
	bool sec = false;       // --sec
	bool tum = false;       // --tum
	double limit = 10.0;    // --limit
	int tekrar = 20;        // --tekrar
	bool goster = false;    // --goster
	bool rapor = false;     // --rapor
	std::string rapor_klasor; // --rapor'dan sonra verilen klasör (boşsa pencereyle seçilir)
};

// Bir bölgenin okuma sonucu
struct Olcum
{
	cv::Rect roi;          // okunan bölge
	std::string kaynak;    // bölgenin nereden geldiği (otomatik, fareyle, ...)
	BarkodSonuc ham;       // sadece ZXing sonucu
	double ham_ms = 0;     // sadece ZXing süresi (ortanca)
	BarkodSonuc sonuc;     // barkod_oku() sonucu
	double sure_ms = 0;    // barkod_oku() süresi (ortanca)
};

bool argumanlari_oku(int argc, char** argv, Secenekler& s)
{
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		// Değer bekleyen seçenekler bir sonraki argümanı okur
		auto deger = [&](const char* ad) -> std::string {
			if (i + 1 >= argc) {
				std::cerr << ad << " için değer eksik\n";
				std::exit(1);
			}
			return argv[++i];
		};
		if (a == "--roi") {
			int x, y, w, h;
			if (std::sscanf(deger("--roi").c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) != 4) {
				std::cerr << "--roi biçimi: x,y,w,h\n";
				return false;
			}
			s.elle_roi = true;
			s.roi = cv::Rect(x, y, w, h);
		} else if (a == "--sec")
			s.sec = true;
		else if (a == "--tum")
			s.tum = true;
		else if (a == "--limit")
			s.limit = std::stod(deger("--limit"));
		else if (a == "--tekrar")
			s.tekrar = std::max(1, std::stoi(deger("--tekrar")));
		else if (a == "--goster")
			s.goster = true;
		else if (a == "--rapor") {
			s.rapor = true;
			// Klasör isteğe bağlı: sonraki argüman bir seçenek değilse klasör kabul et
			if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
				s.rapor_klasor = argv[++i];
		}
		else if (a.rfind("--", 0) == 0) {
			std::cerr << "Bilinmeyen seçenek: " << a << "\n";
			return false;
		} else
			s.dosyalar.push_back(a);
	}
	return true;
}

std::string dosya_sec()
{
#ifdef _WIN32
	// Standart Windows "Aç" penceresi; sadece resim dosyaları listelenir
	char yol[MAX_PATH] = "";
	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = "Resimler\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff\0Tum dosyalar\0*.*\0";
	ofn.lpstrFile = yol;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = "Barkodu okunacak fotografi secin";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameA(&ofn))
		return yol;
#endif
	return {};
}

cv::Mat resim_yukle(const std::string& yol)
{
	// cv::imread Windows'ta Türkçe karakterli yollarda (ör. "Masaüstü") başarısız
	// olabiliyor; dosyayı bayt olarak okuyup imdecode ile çözmek bunu aşıyor
	std::ifstream f(yol, std::ios::binary);
	std::vector<uchar> veri((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (veri.empty())
		return {};
	return cv::imdecode(veri, cv::IMREAD_COLOR);
}

double medyan(std::vector<double> v)
{
	// Ortanca değer: tek seferlik gecikmelerden (önbellek, işletim sistemi) etkilenmez
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

std::string yontem_yaz(const std::vector<std::string>& y)
{
	// Oy veren yöntemleri ayırt edilebilir şekilde birleştir
	std::string s;
	for (size_t i = 0; i < y.size(); ++i)
		s += (i ? " | " : "") + y[i];
	return s;
}

void otomatik_bolge(const cv::Mat& img, bool tum, cv::Rect& roi, std::string& kaynak)
{
	// Varsayılan: tüm fotoğraf
	roi = cv::Rect(0, 0, img.cols, img.rows);
	kaynak = "tüm fotoğraf";
	// --tum verilmediyse kırmızı çerçeveyi ara
	cv::Rect r;
	if (!tum && kirmizi_roi_bul(img, r)) {
		roi = r;
		kaynak = "kırmızı ROI (otomatik)";
	}
}

Olcum olc(const cv::Mat& img, cv::Rect roi, const std::string& kaynak, const Secenekler& s)
{
	Olcum m;
	// Bölgeyi fotoğraf sınırlarına kırp
	m.roi = roi & cv::Rect(0, 0, img.cols, img.rows);
	m.kaynak = kaynak;
	if (m.roi.area() == 0) {
		std::cout << "  [!] Bölge fotoğrafın dışında\n";
		return m;
	}
	cv::Mat crop = img(m.roi);
	std::printf("  Bölge        : %s (%d,%d,%d,%d)\n", kaynak.c_str(), m.roi.x, m.roi.y, m.roi.width,
	            m.roi.height);

	// 1) Sadece ZXing (ön işleme yok); süre birkaç çalıştırmanın ortancası
	m.ham = zxing_ham_oku(crop);
	std::vector<double> sureler;
	for (int i = 0; i < s.tekrar; ++i)
		sureler.push_back(zxing_ham_oku(crop).sure_ms);
	m.ham_ms = medyan(sureler);
	std::printf("  Sadece ZXing : %-15s %.2f ms\n", m.ham.okundu() ? m.ham.metin.c_str() : "OKUNAMADI", m.ham_ms);

	// 2) barkod_oku(): ön işleme + oylama
	BarkodAyar ayar;
	ayar.zaman_limiti_ms = s.limit;
	m.sonuc = barkod_oku(crop, ayar); // ilk çalıştırma (ısınma dahil)
	sureler.clear();
	for (int i = 0; i < s.tekrar; ++i)
		sureler.push_back(barkod_oku(crop, ayar).sure_ms);
	m.sure_ms = medyan(sureler);
	std::printf("  barkod_oku   : %-15s %.2f ms  (%d deneme)\n",
	            m.sonuc.okundu() ? m.sonuc.metin.c_str() : "OKUNAMADI", m.sure_ms, m.sonuc.deneme);
	if (m.sonuc.okundu())
		std::printf("  Yöntem       : %s\n", yontem_yaz(m.sonuc.yontemler).c_str());
	std::printf("  10 ms hedefi : %s\n", m.sonuc.okundu() ? (m.sure_ms < 10.0 ? "TAMAM" : "AŞILDI") : "-");
	return m;
}

cv::Mat ekran_ciz(const cv::Mat& img, const Olcum& m, double& olcek, int& foto_yukseklik)
{
	// Fotoğrafı pencereye sığacak şekilde ölçekle (geniş şerit kareler için)
	olcek = std::min(1.0, 1600.0 / img.cols);
	cv::Mat ekran;
	cv::resize(img, ekran, cv::Size(), olcek, olcek, cv::INTER_AREA);
	foto_yukseklik = ekran.rows;

	// Okunan bölgeyi camgöbeği çerçeveyle işaretle
	cv::Rect r(cvRound(m.roi.x * olcek), cvRound(m.roi.y * olcek), cvRound(m.roi.width * olcek),
	           cvRound(m.roi.height * olcek));
	cv::rectangle(ekran, r, cv::Scalar(255, 255, 0), 2);

	// Okunan bölgenin büyütülmüş hâli (barkod net görünsün)
	if (m.roi.area() > 0) {
		cv::Mat crop = img(m.roi), zoom;
		double z = std::min(220.0 / crop.rows, (double)ekran.cols / crop.cols);
		cv::resize(crop, zoom, cv::Size(), z, z, cv::INTER_NEAREST);
		cv::Mat serit(zoom.rows + 10, ekran.cols, CV_8UC3, cv::Scalar(0, 0, 0));
		zoom.copyTo(serit(cv::Rect((ekran.cols - zoom.cols) / 2, 5, zoom.cols, zoom.rows)));
		cv::vconcat(ekran, serit, ekran);
	}

	// Sonuç şeridi: okunduysa yeşil, okunamadıysa kırmızı
	// (cv::putText Türkçe karakter basamadığı için yazılar ASCII)
	bool ok = m.sonuc.okundu();
	cv::Mat panel(110, ekran.cols, CV_8UC3, ok ? cv::Scalar(0, 110, 0) : cv::Scalar(0, 0, 140));
	char satir1[128], satir2[256];
	std::snprintf(satir1, sizeof(satir1), "%s   %.2f ms", ok ? ("OKUNDU: " + m.sonuc.metin).c_str() : "OKUNAMADI",
	              m.sure_ms);
	std::snprintf(satir2, sizeof(satir2), "Sadece ZXing: %s   |   R: bolge sec   A: otomatik   O: dosya ac   N: sonraki   Q: cikis",
	              m.ham.okundu() ? m.ham.metin.c_str() : "OKUNAMADI");
	cv::putText(panel, satir1, {15, 45}, cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
	cv::putText(panel, satir2, {15, 90}, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(230, 230, 230), 1, cv::LINE_AA);
	cv::vconcat(ekran, panel, ekran);
	return ekran;
}

// Sonuç penceresini gösterir ve tuşları işler.
// Dönüş: 'q' (çıkış), 'o' (yeni dosya açıldı -> yol 'yeni_yol'a yazılır), 'n' (sonraki)
// Sadece tanımlı tuşlara tepki verir; diğer tuşlar yok sayılır.
char goruntule(const cv::Mat& img, Olcum m, const Secenekler& s, std::string& yeni_yol)
{
	while (true) {
		double olcek;
		int foto_y;
		cv::Mat ekran = ekran_ciz(img, m, olcek, foto_y);
		cv::imshow(PENCERE, ekran);
		int tus = cv::waitKey(0);
		// Pencere X ile kapatıldıysa çık
		if (tus < 0 || cv::getWindowProperty(PENCERE, cv::WND_PROP_VISIBLE) < 1)
			return 'q';
		tus &= 0xFF;

		if (tus == 'q' || tus == 'Q' || tus == 27)
			return 'q';

		if (tus == 'r' || tus == 'R') {
			// Sadece fotoğraf kısmında seçim yap (zoom ve panel hariç)
			cv::Rect sec = cv::selectROI(PENCERE, ekran(cv::Rect(0, 0, ekran.cols, foto_y)), true);
			if (sec.area() > 0) {
				// Ekrandaki seçimi orijinal fotoğraf koordinatlarına çevir
				cv::Rect roi(cvRound(sec.x / olcek), cvRound(sec.y / olcek), cvRound(sec.width / olcek),
				             cvRound(sec.height / olcek));
				std::cout << "\n";
				try {
					m = olc(img, roi, "fareyle seçilen bölge", s);
				} catch (const std::exception& e) {
					std::cout << "  [!] Hata: " << e.what() << "\n";
				}
			}
			continue;
		}

		if (tus == 'a' || tus == 'A') {
			// Otomatik bölgeye geri dön
			cv::Rect roi;
			std::string kaynak;
			otomatik_bolge(img, s.tum, roi, kaynak);
			std::cout << "\n";
			try {
				m = olc(img, roi, kaynak, s);
			} catch (const std::exception& e) {
				std::cout << "  [!] Hata: " << e.what() << "\n";
			}
			continue;
		}

		if (tus == 'o' || tus == 'O') {
			// Başka bir fotoğraf seç; iptal edilirse aynı fotoğrafta kal
			yeni_yol = dosya_sec();
			if (!yeni_yol.empty())
				return 'o';
			continue;
		}

		if (tus == 'n' || tus == 'N')
			return 'n';

		// Diğer tuşlar (ENTER, SPACE vb.) yok sayılır: seçim onayından kalan
		// tuş basışları programı kapatmasın
	}
}

// Bir fotoğrafı işler. Dönüş: goruntule() ile aynı ('q', 'o', 'n').
char fotograf_isle(const std::string& yol, const Secenekler& s, std::string& yeni_yol)
{
	std::cout << "\n" << yol << "\n";
	cv::Mat img = resim_yukle(yol);
	if (img.empty()) {
		std::cout << "  [!] Fotoğraf açılamadı\n";
		return 'n';
	}

	// --- Okunacak bölgeyi belirle: elle > fareyle > kırmızı ROI > tüm fotoğraf
	cv::Rect roi;
	std::string kaynak;
	otomatik_bolge(img, s.tum, roi, kaynak);
	if (s.elle_roi) {
		roi = s.roi;
		kaynak = "elle verilen bölge";
	} else if (s.sec) {
		// Açılışta fareyle seçim: ekrana sığacak şekilde ölçeklenmiş fotoğraf üzerinde
		double olcek = std::min(1.0, 1600.0 / img.cols);
		cv::Mat kucuk;
		cv::resize(img, kucuk, cv::Size(), olcek, olcek, cv::INTER_AREA);
		cv::Rect r = cv::selectROI(PENCERE, kucuk, true);
		if (r.area() > 0) {
			roi = cv::Rect(cvRound(r.x / olcek), cvRound(r.y / olcek), cvRound(r.width / olcek),
			               cvRound(r.height / olcek));
			kaynak = "fareyle seçilen bölge";
		}
	}

	Olcum m;
	try {
		m = olc(img, roi, kaynak, s);
	} catch (const std::exception& e) {
		// Beklenmedik bir hata programı kapatmasın; mesajı yaz ve devam et
		std::cout << "  [!] Hata: " << e.what() << "\n";
	}

	// Pencere istenmediyse sonraki fotoğrafa geç
	if (!s.goster)
		return 'n';
	return goruntule(img, m, s, yeni_yol);
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
	// Windows konsolunda Türkçe karakterlerin düzgün görünmesi için UTF-8
	SetConsoleOutputCP(CP_UTF8);
#endif
	Secenekler s;
	if (!argumanlari_oku(argc, argv, s))
		return 1;

	// Rapor modu: klasörü tara, pencere açma (kodu test/rapor.cpp'de)
	if (s.rapor)
		return rapor_calistir(s.rapor_klasor, s.limit, s.tum);

	// Fotoğraf verilmediyse Windows'ta seçme penceresi aç
	if (s.dosyalar.empty()) {
		std::string yol = dosya_sec();
		if (yol.empty()) {
			std::cout << "Kullanım: barkod_oku_cpp foto.jpg [--roi x,y,w,h | --sec | --tum] "
			             "[--limit ms] [--tekrar N] [--goster]\n";
			return 1;
		}
		s.dosyalar.push_back(yol);
		s.goster = true; // pencereden seçildiyse sonucu da pencerede göster
	}
	// --sec pencere gerektirdiği için sonucu da pencerede göster
	if (s.sec)
		s.goster = true;

	std::cout << "zxing-cpp v3.1.1 | süre: " << s.tekrar << " çalıştırmanın ortancası\n";

	// Fotoğrafları sırayla işle; pencerede 'O' ile yeni dosya açılırsa onunla devam et
	std::vector<std::string> kuyruk = s.dosyalar;
	for (size_t i = 0; i < kuyruk.size(); ++i) {
		std::string yeni_yol;
		char durum = fotograf_isle(kuyruk[i], s, yeni_yol);
		if (durum == 'q')
			break;
		if (durum == 'o')
			kuyruk.insert(kuyruk.begin() + i + 1, yeni_yol);
	}
	cv::destroyAllWindows();
	return 0;
}