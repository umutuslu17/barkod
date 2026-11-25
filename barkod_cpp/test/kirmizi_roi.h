/*
 * kirmizi_roi.h
 * =============
 * AMAÇ
 * ----
 * Sistemin kaydettiği karelerde okuma bölgesi (ROI) kırmızı bir çerçeve olarak
 * çizili. Bu modül o çerçeveyi bulur ve içini (çerçeve çizgisi hariç) döner.
 * Sadece TEST içindir: gerçek sistemde crop zaten programın diğer kısmından
 * hazır geliyor, kırmızı çerçeve aramaya gerek yok.
 *
 * İÇERİK
 * ------
 * - kirmizi_roi_bul(): Kırmızı çerçeveyi bulur; bulunursa true döner ve
 *                      çerçevenin iç dikdörtgenini 'roi' içine yazar.
 */
#pragma once

#include <opencv2/core.hpp>

bool kirmizi_roi_bul(const cv::Mat& bgr, cv::Rect& roi);
