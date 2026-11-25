/*
 * barkod_oku.cpp
 * ==============
 * AMAÇ
 * ----
 * barkod_oku.h'de tanımlanan okuma fonksiyonlarının gerçeklemesi.
 * Görüntü ucuzdan pahalıya doğru farklı ön işlemelerden geçirilir, her biri
 * ZXing'e verilir; iki FARKLI yöntem aynı barkodu okuyunca sonuç döner.
 *
 * Akış:
 *   0) Hazırlık : griye çevir; barkod dikeyse 90° döndürüp yatay yap;
 *                 etrafına 20 px beyaz kenar (quiet zone) ekle
 *   Tabanlar    : orijinal + INTER_AREA ile 0.7'ye küçültülmüş (gürültüsü azaltılmış);
 *                 büyük crop'ta önce küçültülmüş taban denenir
 *   Her taban için sırayla (ucuz ve sık işe yarayan adımlar önce):
 *   1) ham      : doğrudan ZXing; iki farklı eşikleme (LocalAverage, GlobalHistogram)
 *   2) x2       : 2x büyüt; keskinleştir; iki farklı eşikleme (LocalAverage, GlobalHistogram)
 *   3) clahe    : yerel kontrastı artır, 2x büyüt, keskinleştir; 2x keskin + Otsu
 *   4) x3       : 3x büyüt, keskinleştir; Otsu ile siyah-beyaz, sonra iki eşikleme
 *   Son olarak her iki taban için:
 *   5) rot      : 2x keskinde ±5°, 3x keskinde ±2,±4,±6,±8° döndür
 *
 * Süre kontrolü: Normal boyutlu bir crop'ta ilk tabanın ham ve 2x adımları (toplam
 * ~2-3 ms) her zaman yapılır; böylece okunabilir bir kare işlemcinin anlık hızından
 * bağımsız olarak okunur. Çok büyük görüntülerde bu adımlar da süre tahminine tabidir.
 * Sonraki adımlar için fonksiyon her ZXing denemesinin süresini ölçüp megapiksel başına
 * ortalama hızı öğrenir. Pahalı bir adıma başlamadan önce, o adımın kalan süreye sığıp
 * sığmayacağını tahmin eder; sığmayacaksa hiç başlamadan "okunamadı" döner.
 * Böylece süre sınırı (varsayılan 10 ms) büyük ölçüde aşılmaz.
 *
 * İÇERİK
 * ------
 * - gri_yap()          : BGR/BGRA görüntüyü tek kanallı griye çevirir.
 * - kenar_ekle()       : Barkodun etrafına beyaz quiet zone ekler.
 * - keskinlestir()     : Unsharp mask (görüntü + k * (görüntü - bulanık)).
 * - dondur()           : Görüntüyü merkezi etrafında küçük bir açıyla döndürür.
 * - dikey_mi()         : Barkodun dikey (çubuklar yatay) durup durmadığını tahmin eder.
 * - ean_gecerli()      : EAN-13 / EAN-8 kontrol hanesini doğrular.
 * - zxing_dene()       : Tek bir görüntüyü zxing-cpp ile okumayı dener.
 * - class Okuyucu      : Denemeleri yürütür, oyları sayar, süreyi takip eder ve
 *                        bir sonraki adımın kalan süreye sığıp sığmayacağını tahmin eder.
 * - barkod_oku_akis()  : Okuma akışının kendisi (aşamalar, oylama).
 * - barkod_oku()       : Dışarıdan çağrılan ana fonksiyon; akışı hata korumasıyla çalıştırır.
 * - zxing_ham_oku()    : Ön işlemesiz, varsayılan ayarlarla tek okuma (karşılaştırma).
 */
#include "barkod_oku.h"

#include <ReadBarcode.h>

#include <opencv2/imgproc.hpp>

// OpenCV 5'te getRotationMatrix2D, contourArea ve boundingRect fonksiyonları
// imgproc'tan yeni "geometry" modülüne taşındı; 4.x'te bu başlık yok.
// Başlık dosyası varsa ekle (sürüm numarasından bağımsız, her iki sürümde çalışır)
#if __has_include(<opencv2/geometry.hpp>)
#include <opencv2/geometry.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <map>

namespace {

using Saat = std::chrono::steady_clock;

// Küçültme oranı: testlerde 0.7 gürültüyü temizleyip barkodu bozmayan denge noktası
constexpr double KUCULTME_ORANI = 0.7;
// Bu yükseklikten büyük crop'lar orijinal kamera çözünürlüğünde sayılır;
// bunlarda önce küçültülmüş taban denenir (hem daha başarılı hem daha hızlı)
constexpr int BUYUK_CROP_YUKSEKLIK = 150;
// Quiet zone için eklenen beyaz kenar (piksel)
constexpr int KENAR_PIKSEL = 20;
// Zorunlu adımlar sadece 2x büyütülmüş hâli bu boyutu (megapiksel) aşmayan
// görüntülerde uygulanır. ROI crop'larında bu değer: 4200x200 kamerada ~0.4 MP,
// 4200x700 kamerada (dikey barkodlu kutular) ~1.3 MP. ROI bulunamayıp tüm kare
// verildiğinde 2 MP'nin üstüne çıkar (4200x200'de ~2.0, 4200x700'de ~6 MP) ve
// zorunlu adımlar tek başına 15-20 ms sürer; o durumda süre tahmini uygulanır
constexpr double ZORUNLU_MAKS_MP = 1.6;
// Yön tahmini: yatay kenar sayısı / dikey kenar sayısı bu değerin altındaysa
// barkod dikey sayılır. Testlerde dikey barkodlu crop'larda oran 0.27-0.43,
// yatay barkodlu crop'larda (arka plandaki bant çizgileri dahil) 0.60 ve üstü
constexpr double DIKEY_ESIK = 0.5;
// Yön tahmini, uzun kenarı yaklaşık bu kadar piksele (en az bu kadar, en fazla iki
// katı) küçültülmüş görüntüde yapılır; oran fotoğrafın çözünürlüğünden bağımsız olur
constexpr int YON_BOYUT = 300;

cv::Mat gri_yap(const cv::Mat& img)
{
	// Zaten tek kanallıysa kopyalamadan döndür
	if (img.channels() == 1)
		return img;
	cv::Mat gri;
	// 4 kanallı (BGRA) ya da 3 kanallı (BGR) görüntüyü griye çevir
	cv::cvtColor(img, gri, img.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
	return gri;
}

cv::Mat kenar_ekle(const cv::Mat& gri)
{
	// Dört taraftan düz beyaz dolgu: crop sıkı kesilse bile EAN'ın istediği
	// boş alan (solda 11, sağda 7 modül) garanti olur
	cv::Mat out;
	cv::copyMakeBorder(gri, out, KENAR_PIKSEL, KENAR_PIKSEL, KENAR_PIKSEL, KENAR_PIKSEL,
	                   cv::BORDER_CONSTANT, cv::Scalar(255));
	return out;
}

cv::Mat keskinlestir(const cv::Mat& img, double sigma, double kuvvet)
{
	// Görüntünün bulanık bir kopyası
	cv::Mat bulanik, out;
	cv::GaussianBlur(img, bulanik, cv::Size(0, 0), sigma);
	// out = img*(1+k) - bulanik*k  ->  kenarlar güçlenir (değerler 0-255'e kırpılır)
	cv::addWeighted(img, 1.0 + kuvvet, bulanik, -kuvvet, 0, out);
	return out;
}

cv::Mat dondur(const cv::Mat& img, double aci)
{
	// Merkez etrafında döndürme matrisi (ölçek 1)
	cv::Point2f merkez(img.cols / 2.0f, img.rows / 2.0f);
	cv::Mat M = cv::getRotationMatrix2D(merkez, aci, 1.0);
	cv::Mat out;
	// Köşelerde siyah oluşmasın diye kenar pikselleri tekrarlanır
	cv::warpAffine(img, out, M, img.size(), cv::INTER_CUBIC, cv::BORDER_REPLICATE);
	return out;
}

bool dikey_mi(const cv::Mat& gri)
{
	// Küçük bir kopya üzerinde çalış: hızlı ve çözünürlükten bağımsız.
	// Küçültme oranı tam sayı seçilir (1/2, 1/3 ...): OpenCV tam sayı oranlı
	// INTER_AREA'yı çok daha hızlı yapıyor (büyük crop'ta ~1 ms yerine ~0.2 ms)
	int bolen = std::max(1, std::max(gri.cols, gri.rows) / YON_BOYUT);
	cv::Mat g = gri;
	if (bolen > 1)
		cv::resize(gri, g, cv::Size(gri.cols / bolen, gri.rows / bolen), 0, 0, cv::INTER_AREA);
	if (g.cols < 2 || g.rows < 2)
		return false;
	// Yan yana iki piksel arasındaki fark: dikey çubukların kenarları (yatay barkod)
	cv::Mat dx, dy;
	cv::absdiff(g.colRange(1, g.cols), g.colRange(0, g.cols - 1), dx);
	// Üst üste iki piksel arasındaki fark: yatay çubukların kenarları (dikey barkod)
	cv::absdiff(g.rowRange(1, g.rows), g.rowRange(0, g.rows - 1), dy);
	// Sadece belirgin kenarları say (zayıf gürültü sayılmasın)
	int nx = cv::countNonZero(dx > 40);
	int ny = cv::countNonZero(dy > 40);
	// Barkod çok sayıda sık kenar üretir; yatay kenarlar baskınsa barkod dikeydir
	return nx < DIKEY_ESIK * std::max(1, ny);
}

bool ean_gecerli(const std::string& m)
{
	// Sadece rakam ve uzunluk 8 ya da 13 olmalı
	if (m.size() != 13 && m.size() != 8)
		return false;
	for (char c : m)
		if (c < '0' || c > '9')
			return false;
	// Sağdan başlayarak (kontrol hanesi hariç) tek konumlar 3, çift konumlar 1 ile çarpılır
	int toplam = 0;
	for (int i = 0; i < (int)m.size() - 1; ++i) {
		int r = m[m.size() - 2 - i] - '0';
		toplam += (i % 2 == 0) ? 3 * r : r;
	}
	return (10 - toplam % 10) % 10 == m.back() - '0';
}

std::string zxing_dene(const cv::Mat& img, ZXing::Binarizer binarizer)
{
	// Sadece market barkodları. ZXing'in kendi 90° döndürme ve küçültme denemeleri
	// kapalı: dikey barkodu akışın başında bir kez biz döndürüyoruz (her denemede
	// iki yönü birden aramak süreyi ikiye katlardı), küçültmeyi de kontrollü yapıyoruz
	auto ayar = ZXing::ReaderOptions()
	                .setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8)
	                .setTryRotate(false)
	                .setTryDownscale(false)
	                .setBinarizer(binarizer);
	// cv::Mat'i kopyalamadan ZXing'in görüntü görünümüne sar (satır adımı dahil)
	ZXing::ImageView gorunum(img.data, img.cols, img.rows, ZXing::ImageFormat::Lum, (int)img.step);
	// Kontrol hanesi doğru olan ilk geçerli sonucu döndür. Okuma sırasında
	// beklenmedik bir hata olursa bu deneme "okunamadı" sayılır; fonksiyon
	// hiçbir durumda üretim programını çökertmemeli
	try {
		for (const auto& b : ZXing::ReadBarcodes(gorunum, ayar))
			if (b.isValid() && ean_gecerli(b.text()))
				return b.text();
	} catch (...) {
	}
	return {};
}

// Denemeleri yürüten yardımcı sınıf: her deneme bir oy üretebilir; yeterli oy
// toplanınca ya da süre dolunca dene() true döner ve akış kesilir.
class Okuyucu
{
public:
	Okuyucu(const BarkodAyar& ayar) : ayar_(ayar), baslangic_(Saat::now()) {}

	// Bir görüntüyü dener. true dönerse işlem bitti (sonuç bulundu ya da süre doldu).
	bool dene(const std::string& ad, const cv::Mat& img,
	          ZXing::Binarizer bin = ZXing::Binarizer::LocalAverage)
	{
		++sonuc_.deneme;
		auto t0 = Saat::now();
		std::string m = zxing_dene(img, bin);
		// ZXing'in megapiksel başına harcadığı süreyi öğren. Ortalama kullanılır:
		// tek bir yavaş ölçüm (işlemcinin anlık yavaşlaması) tahmini bozmasın
		double ms = std::chrono::duration<double, std::milli>(Saat::now() - t0).count();
		double mp = std::max(1e-3, img.total() / 1e6);
		hiz_ = (hiz_ * olcum_ + ms / mp) / (olcum_ + 1);
		++olcum_;
		if (!m.empty()) {
			// Bu yöntemin okuduğu sonucu oylara ekle
			auto& liste = oylar_[m];
			liste.push_back(ad);
			// Yeterince farklı yöntem aynı sonucu verdiyse kabul et
			if ((int)liste.size() >= ayar_.gereken_oy) {
				sonuc_.metin = m;
				sonuc_.yontemler = liste;
				return true;
			}
		}
		// Süre sınırı aşıldıysa bu karede pes et (sonraki karede tekrar denenir).
		// Zorunlu adımlarda süreye bakılmaz (bkz. zorunlu())
		return !zorunlu_ && ayar_.zaman_limiti_ms > 0 && gecen_ms() > ayar_.zaman_limiti_ms;
	}

	// Zorunlu mod: açıkken süre sınırı denemeleri durdurmaz. En ucuz ve en sık
	// işe yarayan ilk adımlar bu modda çalışır; böylece sonuç işlemcinin anlık
	// hızına bağlı olmaz (aynı kare her çalıştırmada aynı sonucu verir)
	void zorunlu(bool acik) { zorunlu_ = acik; }

	// Bir sonraki adım (görüntü hazırlama + ZXing) kalan süreye sığar mı?
	// mp: adımın üreteceği görüntünün megapiksel cinsinden boyutu
	// carpan: görüntü hazırlamanın (büyütme, keskinleştirme vb.) ek maliyeti için pay
	bool sigar(double mp, double carpan) const
	{
		if (ayar_.zaman_limiti_ms <= 0)
			return true;
		return gecen_ms() + hiz_ * mp * carpan <= ayar_.zaman_limiti_ms;
	}

	// Süre sınırı aşıldı mı? (zorunlu adımlardan sonra kontrol için)
	bool sure_doldu() const { return gecen_ms() > ayar_.zaman_limiti_ms; }

	BarkodSonuc bitir()
	{
		sonuc_.sure_ms = gecen_ms();
		return sonuc_;
	}

private:
	double gecen_ms() const
	{
		return std::chrono::duration<double, std::milli>(Saat::now() - baslangic_).count();
	}

	BarkodAyar ayar_;
	Saat::time_point baslangic_;
	std::map<std::string, std::vector<std::string>> oylar_;
	double hiz_ = 0.0;     // ZXing'in ortalama ms/megapiksel hızı (ölçülerek öğrenilir)
	int olcum_ = 0;        // hız ortalamasına giren ölçüm sayısı
	bool zorunlu_ = false; // true iken süre sınırı denemeleri durdurmaz
	BarkodSonuc sonuc_;
};

// Bir taban görüntü ve ondan türetilen (bir kez hesaplanıp tekrar kullanılan) görüntüler
struct Taban
{
	std::string on_ek; // yöntem adına eklenen ön ek ("k0.7+" ya da "")
	cv::Mat g;         // taban görüntü
	cv::Mat k2, k3;    // 2x ve 3x büyütülmüş + keskinleştirilmiş hâlleri
};

} // namespace

// Asıl akış; barkod_oku() bunu hata korumasıyla çağırır
static BarkodSonuc barkod_oku_akis(const cv::Mat& crop, const BarkodAyar& ayar)
{
	Okuyucu o(ayar);
	// Boş girdi kontrolü
	if (crop.empty())
		return o.bitir();

	// 0) Hazırlık: gri, gerekirse yatay hâle döndür, quiet zone ekle
	cv::Mat gri = gri_yap(crop);
	bool dikey = (ayar.yon == BarkodYon::Dikey) || (ayar.yon == BarkodYon::Otomatik && dikey_mi(gri));
	if (dikey) {
		// Dikey barkodu 90° çevirerek yatay yap; sonraki tüm adımlar aynen çalışır.
		// (EAN iki yönden de okunabildiği için saat yönü ya da tersi fark etmez)
		cv::Mat d;
		cv::rotate(gri, d, cv::ROTATE_90_CLOCKWISE);
		gri = d;
	}
	// Büyük/küçük crop kararı, döndürülmüş ve kenar eklenmemiş yüksekliğe göre
	const int crop_yukseklik = gri.rows;
	gri = kenar_ekle(gri);
	// Yöntem adlarının başına "dik+" eklenir; raporda dikey okunduğu görünsün
	const std::string yon_ek = dikey ? "dik+" : "";

	// İki taban: orijinal ve INTER_AREA ile küçültülmüş (komşu piksellerin
	// ortalaması alınarak gürültü azaltılır)
	Taban orijinal{yon_ek, gri, {}, {}};
	Taban kucuk{yon_ek + "k0.7+", {}, {}, {}};
	cv::resize(gri, kucuk.g, cv::Size(), KUCULTME_ORANI, KUCULTME_ORANI, cv::INTER_AREA);
	// Büyük crop'ta önce küçük taban, küçük crop'ta önce orijinal denenir.
	// Karar, beyaz kenar eklenmeden ÖNCEKİ crop yüksekliğine göre verilir; aksi hâlde
	// düşük çözünürlüklü küçük crop'lar yanlışlıkla "büyük" sayılıp önce küçültülüyor
	std::vector<Taban*> tabanlar = (crop_yukseklik > BUYUK_CROP_YUKSEKLIK)
	                                   ? std::vector<Taban*>{&kucuk, &orijinal}
	                                   : std::vector<Taban*>{&orijinal, &kucuk};

	// Deneme sırası: her taban için ucuz ve en sık işe yarayan adımlar önce,
	// nadiren işe yarayan pahalı adımlar sonra. Testlerde zor kareleri hep
	// CLAHE ve Otsu kurtardığı için bunlar 2x büyütmeyle (3x'e göre ~2 kat ucuz)
	// 3x büyütme denemelerinin önüne alındı; böylece zor kareler de 10 ms içinde okunabiliyor.
	// Her pahalı adımdan önce o.sigar() ile kalan sürenin yetip yetmeyeceğine bakılır;
	// ikinci sayı, görüntü hazırlamanın (büyütme, keskinleştirme, CLAHE) ek maliyet payıdır.
	auto clahe = cv::createCLAHE(2.0, cv::Size(4, 4));
	for (Taban* t : tabanlar) {
		// İlk tabanın ham + 2x adımları ZORUNLU: süre tahmini ne derse desin yapılır.
		// Okunan karelerin neredeyse hepsi bu adımlarda okunuyor ve normal boyutlu bir
		// crop'ta toplamları ~2-3 ms; böylece okunabilir bir kare, işlemci anlık yavaşlasa
		// da okunmadan geçilmez. Çok büyük görüntülerde (ör. ROI yerine tüm kare) bu adımlar
		// pahalı olduğu için zorunlu tutulmaz, onlarda da süre tahmini uygulanır.
		bool ilk_taban = (t == tabanlar.front()) && (4.0 * t->g.total() / 1e6 <= ZORUNLU_MAKS_MP);
		o.zorunlu(ilk_taban);

		// 1) Ham deneme: barkodun ortada olduğu kolay kareler burada okunur
		if (o.dene(t->on_ek + "ham", t->g))
			return o.bitir();
		// Aynı görüntü, ZXing'in diğer eşikleme yöntemiyle: yeni görüntü işlemi yok,
		// çok ucuz. Barkodu iyi görünen karelerde ikinci oyu büyütmeye gerek kalmadan verir
		if (o.dene(t->on_ek + "ham+GH", t->g, ZXing::Binarizer::GlobalHistogram))
			return o.bitir();

		// Taban görüntünün megapiksel boyutu (sonraki adımların maliyet tahmini için)
		double mp = t->g.total() / 1e6;

		// 2) 2x büyütme + keskinleştirme + iki eşikleme yöntemi
		//    (ilk tabanda zorunlu; ikinci tabanda önce süreye sığıp sığmadığına bakılır)
		if (!ilk_taban && !o.sigar(4 * mp, 2.0))
			return o.bitir();
		cv::Mat b2;
		cv::resize(t->g, b2, cv::Size(), 2, 2, cv::INTER_CUBIC);
		if (o.dene(t->on_ek + "x2", b2))
			return o.bitir();
		t->k2 = keskinlestir(b2, 0.8 * 2, 1.0);
		if (o.dene(t->on_ek + "x2+keskin", t->k2))
			return o.bitir();
		// Aynı görüntü, ZXing'in diğer eşikleme yöntemiyle (yeni görüntü işlemi yok)
		if (o.dene(t->on_ek + "x2+keskin+GH", t->k2, ZXing::Binarizer::GlobalHistogram))
			return o.bitir();
		// Zorunlu adımlar bitti; bundan sonrası süre sınırına tabi
		o.zorunlu(false);
		if (ayar.zaman_limiti_ms > 0 && o.sure_doldu())
			return o.bitir();

		// 3) Kontrast: CLAHE ile yerel kontrast, sonra 2x büyüt ve keskinleştir;
		//    ardından 2x keskin görüntüyü Otsu ile kesin siyah-beyaza çevir
		if (!o.sigar(4 * mp, 2.0))
			return o.bitir();
		cv::Mat c, c2;
		clahe->apply(t->g, c);
		cv::resize(c, c2, cv::Size(), 2, 2, cv::INTER_CUBIC);
		if (o.dene(t->on_ek + "clahe+x2+keskin", keskinlestir(c2, 1.6, 1.0)))
			return o.bitir();
		{
			cv::Mat yum2, otsu2;
			cv::GaussianBlur(t->k2, yum2, cv::Size(3, 3), 0);
			cv::threshold(yum2, otsu2, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
			if (o.dene(t->on_ek + "x2+keskin+otsu", otsu2))
				return o.bitir();
		}

		// 4) 3x büyüt + keskinleştir; önce Otsu ile kesin siyah-beyaz (daha sık işe yarıyor)
		if (!o.sigar(9 * mp, 3.0))
			return o.bitir();
		cv::Mat b3, yumusak, otsu;
		cv::resize(t->g, b3, cv::Size(), 3, 3, cv::INTER_CUBIC);
		t->k3 = keskinlestir(b3, 0.8 * 3, 1.0);
		cv::GaussianBlur(t->k3, yumusak, cv::Size(3, 3), 0);
		cv::threshold(yumusak, otsu, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
		if (o.dene(t->on_ek + "x3+keskin+otsu", otsu))
			return o.bitir();
		if (o.dene(t->on_ek + "x3", b3))
			return o.bitir();
		if (o.dene(t->on_ek + "x3+keskin", t->k3))
			return o.bitir();
		if (o.dene(t->on_ek + "x3+keskin+GH", t->k3, ZXing::Binarizer::GlobalHistogram))
			return o.bitir();
	}

	// 5) Açı düzeltme (en pahalı, en son): her açı hazır keskin görüntüden bir kez döndürülür
	for (Taban* t : tabanlar) {
		if (t->k3.empty())
			continue; // bu tabanın 3x aşaması süre yüzünden hiç yapılmadı
		if (!o.sigar(9 * t->g.total() / 1e6, 3.0))
			return o.bitir();
		for (double aci : {5.0, -5.0})
			if (o.dene(t->on_ek + "x2+keskin+rot" + std::to_string((int)aci), dondur(t->k2, aci)))
				return o.bitir();
		for (double aci : {4.0, -4.0, 6.0, -6.0, 2.0, -2.0, 8.0, -8.0})
			if (o.dene(t->on_ek + "x3+keskin+rot" + std::to_string((int)aci), dondur(t->k3, aci)))
				return o.bitir();
	}

	// Hiçbir sonuç yeterli oyu almadı
	return o.bitir();
}

BarkodSonuc barkod_oku(const cv::Mat& crop, const BarkodAyar& ayar)
{
	// Görüntü işleme sırasında beklenmedik bir hata (ör. bozuk/çok küçük crop)
	// olursa "okunamadı" sonucu dön; çağıran program çökmesin
	try {
		return barkod_oku_akis(crop, ayar);
	} catch (...) {
		return BarkodSonuc{};
	}
}

BarkodSonuc zxing_ham_oku(const cv::Mat& crop)
{
	auto t0 = Saat::now();
	BarkodSonuc s;
	// Gri görüntüyü ön işlemesiz, kütüphanenin varsayılan ayarlarıyla oku
	// (tryHarder, tryRotate, tryDownscale açık; sadece format EAN ile sınırlı)
	cv::Mat gri = gri_yap(crop);
	auto ayar = ZXing::ReaderOptions().setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8);
	ZXing::ImageView gorunum(gri.data, gri.cols, gri.rows, ZXing::ImageFormat::Lum, (int)gri.step);
	try {
		for (const auto& b : ZXing::ReadBarcodes(gorunum, ayar))
			if (b.isValid()) {
				s.metin = b.text();
				s.yontemler = {"zxing varsayilan"};
				break;
			}
	} catch (...) {
	}
	s.deneme = 1;
	s.sure_ms = std::chrono::duration<double, std::milli>(Saat::now() - t0).count();
	return s;
}
