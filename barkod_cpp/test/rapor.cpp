/*
 * rapor.cpp
 * =========
 * AMAÇ
 * ----
 * Test programının RAPOR MODU (barkod_oku_cpp --rapor [klasör]).
 * Bir klasörü ve tüm alt klasörlerini tarar; her alt klasörü bir kutu sayar.
 * Her kareyi gerçek kullanımdaki gibi okur (kırmızı ROI, tek okuma, süre sınırı)
 * ve her kutunun EN AZ BİR karesinde barkodun okunup okunmadığını raporlar.
 * Ayrıca tüm fotoğraflar üzerinden okunma yüzdesini verir; düz bir klasörde
 * (alt klasörsüz, ör. "en iyi kareler" klasörü) asıl sonuç bu yüzdedir; her
 * fotoğraf için okundu/okunamadı satırı yazılır ve okunamayanlar ayrıca listelenir.
 * Kare kare sonuçlar klasöre barkod_rapor_cpp.csv olarak yazılır.
 *
 * Sadece TEST içindir; teslim edilecek barkod_oku.h/.cpp bu dosyaya bağımlı değildir.
 *
 * İÇERİK
 * ------
 * - klasor_sec()      : (Windows) klasör seçme penceresini açar.
 * - yazdir()          : Dosya yolunu ekrana/CSV'ye yazılabilir UTF-8 metne çevirir.
 * - resim_yukle()     : Türkçe karakterli yollarda da çalışan resim okuma.
 * - otomatik_bolge()  : Kırmızı ROI'yi bulur, yoksa tüm fotoğrafı döner.
 * - dogal_kucuk()     : Dosya adlarını sayı sırasına göre karşılaştırır (2.jpg < 10.jpg).
 * - FotoSonuc         : Bir fotoğrafın okuma sonucu (düz klasör listesinde kullanılır).
 * - KutuOzet          : Bir kutunun (alt klasörün) istatistikleri.
 * - rapor_calistir()  : Klasörü tarar, her kareyi okur, kutu özetini ve CSV'yi yazar.
 */
#include "rapor.h"

#include "barkod_oku.h"
#include "kirmizi_roi.h"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace {

namespace fs = std::filesystem;

std::string klasor_sec()
{
#ifdef _WIN32
	// Standart Windows "Klasöre Gözat" penceresi
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	char ad[MAX_PATH] = "";
	BROWSEINFOA bi = {};
	bi.pszDisplayName = ad;
	bi.lpszTitle = "Kutu klasorlerini iceren klasoru secin (orn. error)";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
	if (pidl) {
		char yol[MAX_PATH] = "";
		bool ok = SHGetPathFromIDListA(pidl, yol);
		CoTaskMemFree(pidl);
		if (ok)
			return yol;
	}
#endif
	return {};
}

std::string yazdir(const fs::path& p)
{
	// Yolu UTF-8 metne çevir (Türkçe karakterli klasör adları konsolda ve CSV'de düzgün görünsün)
	auto u = p.u8string();
	return std::string(u.begin(), u.end());
}

cv::Mat resim_yukle(const fs::path& yol)
{
	// cv::imread Windows'ta Türkçe karakterli yollarda (ör. "Masaüstü") başarısız
	// olabiliyor; dosyayı bayt olarak okuyup imdecode ile çözmek bunu aşıyor
	std::ifstream f(yol, std::ios::binary);
	std::vector<uchar> veri((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (veri.empty())
		return {};
	return cv::imdecode(veri, cv::IMREAD_COLOR);
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

// İki yolu "doğal" sırayla karşılaştırır: içlerindeki sayılar sayı olarak
// karşılaştırılır, böylece 2.1.jpg, 10.1.jpg'den önce gelir (alfabetik sırada sonra gelirdi)
bool dogal_kucuk(const fs::path& a, const fs::path& b)
{
	std::string x = a.string(), y = b.string();
	size_t i = 0, j = 0;
	while (i < x.size() && j < y.size()) {
		if (std::isdigit((unsigned char)x[i]) && std::isdigit((unsigned char)y[j])) {
			// Her iki tarafta da bir sayı başlıyor: sayıları bütün olarak oku ve karşılaştır
			size_t i2 = i, j2 = j;
			while (i2 < x.size() && std::isdigit((unsigned char)x[i2]))
				++i2;
			while (j2 < y.size() && std::isdigit((unsigned char)y[j2]))
				++j2;
			unsigned long long nx = std::stoull(x.substr(i, std::min<size_t>(i2 - i, 18)));
			unsigned long long ny = std::stoull(y.substr(j, std::min<size_t>(j2 - j, 18)));
			if (nx != ny)
				return nx < ny;
			i = i2;
			j = j2;
		} else {
			// Sayı değilse karakter karakter karşılaştır
			if (x[i] != y[j])
				return x[i] < y[j];
			++i;
			++j;
		}
	}
	return x.size() - i < y.size() - j;
}

// Bir fotoğrafın okuma sonucu (düz klasörde her fotoğraf için satır yazmak için)
struct FotoSonuc
{
	std::string ad;     // klasöre göre yol
	std::string barkod; // okunan barkod; okunamadıysa boş
	double sure_ms = 0; // okuma süresi
};

// Rapor modunda bir kutunun (alt klasörün) istatistikleri
struct KutuOzet
{
	int kare = 0;                    // işlenen kare sayısı
	int okunan = 0;                  // barkod_oku()'nun okuduğu kare sayısı
	std::string ilk_kare;            // barkodun ilk okunduğu kare
	double ilk_sure = 0;             // o karedeki okuma süresi
	double okunan_max = 0;           // okunan karelerde en uzun süre
	double okunan_top = 0;           // okunan karelerde toplam süre (ortalama için)
	double okunamayan_top = 0;       // okunamayan karelerde toplam süre (ortalama için)
	int okunamayan = 0;              // okunamayan kare sayısı
	std::map<std::string, int> barkodlar;     // barkod_oku() sonuçları ve kaç kez
};

} // namespace

int rapor_calistir(const std::string& klasor_arg, double limit, bool tum)
{
	// Klasör verilmediyse seçme penceresi aç
	std::string klasor = klasor_arg.empty() ? klasor_sec() : klasor_arg;
	fs::path kok(klasor);
	if (klasor.empty() || !fs::is_directory(kok)) {
		std::cout << "Klasör bulunamadı ya da seçilmedi: " << klasor << "\n";
		return 1;
	}

	// Klasör ve tüm alt klasörlerdeki resim dosyalarını topla, bant sırasıyla gelsin diye sırala
	const std::set<std::string> uzantilar{".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff"};
	std::vector<fs::path> dosyalar;
	for (const auto& e : fs::recursive_directory_iterator(kok, fs::directory_options::skip_permission_denied)) {
		if (!e.is_regular_file())
			continue;
		std::string uz = e.path().extension().string();
		std::transform(uz.begin(), uz.end(), uz.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (uzantilar.count(uz))
			dosyalar.push_back(e.path());
	}
	std::sort(dosyalar.begin(), dosyalar.end(), dogal_kucuk);
	if (dosyalar.empty()) {
		std::cout << "Klasörde resim bulunamadı: " << yazdir(kok) << "\n";
		return 1;
	}
	std::cout << yazdir(kok) << "\n" << dosyalar.size() << " fotoğraf bulundu, süre sınırı " << limit
	          << " ms. Taranıyor...\n";

	// Kare kare sonuçlar CSV'ye (Excel Türkçe karakterleri doğru açsın diye UTF-8 BOM ile)
	fs::path csv_yol = kok / "barkod_rapor_cpp.csv";
	std::ofstream csv(csv_yol, std::ios::binary);
	csv << "\xEF\xBB\xBF" << "kutu;dosya;barkod_oku;sure_ms;deneme;yontem\n";

	BarkodAyar ayar;
	ayar.zaman_limiti_ms = limit;
	std::map<std::string, KutuOzet> kutular;
	int asan = 0; // 10 ms'yi aşan kare sayısı
	std::vector<std::string> okunamayan_fotolar; // okunamayan fotoğrafların göreli yolları
	std::vector<FotoSonuc> fotolar;              // tüm fotoğrafların sonuçları (sırayla)
	double genel_max = 0;

	for (size_t i = 0; i < dosyalar.size(); ++i) {
		const fs::path& f = dosyalar[i];
		cv::Mat img = resim_yukle(f);
		if (img.empty())
			continue;

		// Gerçek kullanımı taklit et: kırmızı ROI (yoksa tüm kare), tek okuma, süre sınırlı
		cv::Rect roi;
		std::string kaynak;
		otomatik_bolge(img, tum, roi, kaynak);
		cv::Mat crop = img(roi & cv::Rect(0, 0, img.cols, img.rows));
		BarkodSonuc r;
		try {
			r = barkod_oku(crop, ayar);
		} catch (const std::exception& e) {
			std::cout << "  [!] " << yazdir(f) << ": " << e.what() << "\n";
		}

		// Fotoğrafın bulunduğu alt klasör = kutu
		std::string kutu = yazdir(fs::relative(f.parent_path(), kok));
		if (kutu.empty())
			kutu = ".";
		KutuOzet& k = kutular[kutu];
		++k.kare;
		std::string ad = yazdir(f.filename());
		if (r.okundu()) {
			if (k.okunan == 0) {
				k.ilk_kare = ad;
				k.ilk_sure = r.sure_ms;
			}
			++k.okunan;
			k.okunan_max = std::max(k.okunan_max, r.sure_ms);
			k.okunan_top += r.sure_ms;
			++k.barkodlar[r.metin];
		} else {
			++k.okunamayan;
			k.okunamayan_top += r.sure_ms;
			// Fotoğraf bazlı liste (düz klasör modunda tek tek gösterilir)
			okunamayan_fotolar.push_back(yazdir(fs::relative(f, kok)));
		}
		// Her fotoğrafın sonucunu sakla (düz klasör listesi için)
		fotolar.push_back({yazdir(fs::relative(f, kok)), r.okundu() ? r.metin : "", r.sure_ms});
		if (r.sure_ms > 10.0)
			++asan;
		genel_max = std::max(genel_max, r.sure_ms);

		// CSV satırı
		std::string yontem;
		for (size_t j = 0; j < r.yontemler.size(); ++j)
			yontem += (j ? " | " : "") + r.yontemler[j];
		char sure[32];
		std::snprintf(sure, sizeof(sure), "%.2f", r.sure_ms);
		csv << kutu << ";" << ad << ";" << (r.okundu() ? r.metin : "") << ";" << sure << ";" << r.deneme << ";"
		    << yontem << "\n";

		// İlerleme göstergesi (her 50 karede bir)
		if ((i + 1) % 50 == 0 || i + 1 == dosyalar.size())
			std::cout << "  " << (i + 1) << "/" << dosyalar.size() << " kare işlendi\n";
	}

	// --- Kutu kutu sonuçlar
	// Alt klasör yoksa (tüm fotoğraflar tek klasörde) kutu listesi anlamsız; sadece özet yazılır
	bool duz_klasor = kutular.size() == 1 && kutular.count(".") == 1;
	if (duz_klasor) {
		// Düz klasör: her fotoğraf için okundu / okunamadı satırı
		std::cout << "\n========== FOTOĞRAF SONUÇLARI ==========\n";
		for (const auto& fs_ : fotolar) {
			if (!fs_.barkod.empty())
				std::printf("  [OKUNDU]    %-30s %6.1f ms  -> %s\n", fs_.ad.c_str(), fs_.sure_ms, fs_.barkod.c_str());
			else
				std::printf("  [OKUNAMADI] %-30s %6.1f ms\n", fs_.ad.c_str(), fs_.sure_ms);
		}
	}
	if (!duz_klasor)
		std::cout << "\n========== KUTU SONUÇLARI ==========\n";
	int okunan_kutu = 0, coklu = 0;
	double okunan_max = 0, okunan_top = 0, okunamayan_top = 0;
	int okunan_say = 0, okunamayan_say = 0;
	std::vector<std::string> okunmayanlar;
	for (const auto& [ad, k] : kutular) {
		// Kutunun barkodu: barkod_oku()'nun en çok okuduğu değer
		std::string barkod;
		int en = 0;
		for (const auto& [b, n] : k.barkodlar)
			if (n > en) {
				en = n;
				barkod = b;
			}
		if (k.okunan > 0)
			++okunan_kutu;
		else
			okunmayanlar.push_back(ad);
		if (k.barkodlar.size() > 1)
			++coklu; // aynı kutuda farklı barkodlar okunmuş: incelenmeli
		// Genel süre istatistikleri için kutunun toplamlarını biriktir
		okunan_max = std::max(okunan_max, k.okunan_max);
		okunan_top += k.okunan_top;
		okunan_say += k.okunan;
		okunamayan_top += k.okunamayan_top;
		okunamayan_say += k.okunamayan;

		if (duz_klasor)
			continue; // düz klasörde kutu satırı yazılmaz
		if (k.okunan > 0)
			std::printf("  [OK]  %s : %d/%d kare, ilk %s (%.1f ms) -> %s\n", ad.c_str(), k.okunan, k.kare,
			            k.ilk_kare.c_str(), k.ilk_sure, barkod.c_str());
		else
			std::printf("  [YOK] %s : 0/%d kare OKUNAMADI\n", ad.c_str(), k.kare);
		if (k.barkodlar.size() > 1)
			std::printf("        [!] bu kutuda %zu farklı barkod okundu, CSV'den kontrol edin\n", k.barkodlar.size());
	}

	// --- Genel özet
	int toplam_kare = 0;
	for (const auto& [ad, k] : kutular)
		toplam_kare += k.kare;
	std::cout << "\n========== ÖZET ==========\n";
	// Fotoğraf bazında okunma oranı (düz klasörde asıl sonuç budur)
	std::printf("Fotoğraf      : %d/%d fotoğrafta barkod okundu (%%%.1f)\n", okunan_say, toplam_kare,
	            toplam_kare ? 100.0 * okunan_say / toplam_kare : 0.0);
	if (!duz_klasor)
		std::printf("Kutu          : %d/%zu kutu en az bir karede okundu (%%%.1f)\n", okunan_kutu, kutular.size(),
		            kutular.empty() ? 0.0 : 100.0 * okunan_kutu / kutular.size());
	if (coklu > 0)
		std::printf("[!] %d kutuda barkod_oku() birden fazla farklı barkod okudu\n", coklu);
	// Barkodun okunduğu karelerde ortalama ve en uzun süre
	std::printf("Süre          : okunan karede ortalama %.1f ms, en fazla %.1f ms (%d kare)\n",
	            okunan_say ? okunan_top / okunan_say : 0.0, okunan_max, okunan_say);
	std::printf("                okunamayan karede ortalama %.1f ms\n",
	            okunamayan_say ? okunamayan_top / okunamayan_say : 0.0);
	std::printf("                %d/%d kare 10 ms'yi aştı (en uzun %.1f ms)\n", asan, toplam_kare, genel_max);
	if (duz_klasor && !okunamayan_fotolar.empty()) {
		std::cout << "Okunamayan fotoğraflar:\n";
		for (const auto& a : okunamayan_fotolar)
			std::cout << "  - " << a << "\n";
	}
	if (!duz_klasor && !okunmayanlar.empty()) {
		std::cout << "Okunamayan kutular:\n";
		for (const auto& a : okunmayanlar)
			std::cout << "  - " << a << "\n";
	}
	std::cout << "Rapor (kare kare): " << yazdir(csv_yol) << "\n";
	return 0;
}