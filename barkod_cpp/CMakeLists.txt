# CMakeLists.txt
# ==============
# AMAÇ: barkod_oku() fonksiyonunu test eden barkod_oku_cpp programını derler.
#  - teslim/ : teslim edilecek fonksiyon (barkod_oku.h, barkod_oku.cpp)
#  - test/   : sadece test için olan program (main.cpp, kirmizi_roi.*, rapor.*)
#  - zxing-cpp v3.1.1 (en son sürüm) GitHub'dan otomatik indirilip programla
#    birlikte derlenir (FetchContent). Ayrı kurulum gerekmez.
#  - OpenCV önceden kurulu olmalı; yeri OpenCV_DIR ile verilir
#    (örn. -DOpenCV_DIR=C:/opencv/build).
#
# Derleme (Release önemli: Debug derleme okumayı ~10 kat yavaşlatır):
#   cmake -S . -B build -A x64 -DOpenCV_DIR=C:/opencv/build
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.21)
project(barkod_oku_cpp LANGUAGES CXX)

# zxing-cpp 3.x C++20 istiyor
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tek yapılandırmalı üreticilerde (Ninja, MinGW) varsayılan Release olsun
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# MSVC: kaynak dosyalar UTF-8 (Türkçe karakterler için)
if(MSVC)
  add_compile_options(/utf-8)
endif()

# --- zxing-cpp: sadece okuyucu kısmı, örnekler ve testler kapalı
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  zxing
  GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
  GIT_TAG        v3.1.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(zxing)

# OpenCV_DIR verilmemişse varsayılan kurulum yerini kullan
if(NOT OpenCV_DIR AND NOT DEFINED ENV{OpenCV_DIR} AND EXISTS "C:/opencv/build/OpenCVConfig.cmake")
  set(OpenCV_DIR "C:/opencv/build")
endif()

# --- OpenCV: 4.x ve 5.x ile çalışsın diye modül listesi sürüme göre belirlenir
# (OpenCV 5'te bazı geometri fonksiyonları yeni "geometry" modülüne taşındı)
find_package(OpenCV REQUIRED COMPONENTS core)
set(OPENCV_MODULLER core imgproc imgcodecs highgui)
if(OpenCV_VERSION_MAJOR GREATER_EQUAL 5)
  list(APPEND OPENCV_MODULLER geometry)
endif()
find_package(OpenCV REQUIRED COMPONENTS ${OPENCV_MODULLER})

# --- Program
add_executable(barkod_oku_cpp
  teslim/barkod_oku.cpp
  test/main.cpp
  test/kirmizi_roi.cpp
  test/rapor.cpp
)
# test/main.cpp, teslim/ klasöründeki barkod_oku.h'yi bulabilsin
target_include_directories(barkod_oku_cpp PRIVATE teslim)
target_link_libraries(barkod_oku_cpp PRIVATE ZXing::ZXing ${OpenCV_LIBS})
if(WIN32)
  # Dosya seçme penceresi (GetOpenFileName) için
  target_link_libraries(barkod_oku_cpp PRIVATE comdlg32)
  # OpenCV DLL'lerini exe'nin yanına kopyala (PATH ayarına gerek kalmasın).
  # Liste boşsa (statik OpenCV) betik hiçbir şey yapmaz, hata vermez.
  add_custom_command(TARGET barkod_oku_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DDLLS=$<TARGET_RUNTIME_DLLS:barkod_oku_cpp>"
            "-DHEDEF=$<TARGET_FILE_DIR:barkod_oku_cpp>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dll_kopyala.cmake)
endif()