# dll_kopyala.cmake
# =================
# AMAÇ: Derlemeden sonra programın ihtiyaç duyduğu DLL'leri (OpenCV) exe'nin
# bulunduğu klasöre kopyalar; böylece Windows'ta PATH ayarlamaya gerek kalmaz.
# CMakeLists.txt tarafından -P ile çağrılır.
#   DLLS  : kopyalanacak DLL yolları (noktalı virgülle ayrılmış, boş olabilir)
#   HEDEF : exe'nin klasörü

# Her DLL'i tek tek kopyala; liste boşsa döngü hiç çalışmaz
foreach(dll IN LISTS DLLS)
  if(EXISTS "${dll}")
    # Sadece değişmişse kopyala (her derlemede gereksiz kopyalama yapılmasın)
    file(COPY "${dll}" DESTINATION "${HEDEF}")
  endif()
endforeach()
