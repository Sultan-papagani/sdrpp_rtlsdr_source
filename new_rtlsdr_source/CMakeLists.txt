cmake_minimum_required(VERSION 3.13)
project(new_rtlsdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_include_directories(new_rtlsdr_source PRIVATE "C:/Users/TOSHIBA/Desktop/rtlsdrlib/rtlsdr/include")
    target_link_directories(new_rtlsdr_source PRIVATE "C:/Users/TOSHIBA/Desktop/rtlsdrlib/rtlsdr/")

    target_link_libraries(new_rtlsdr_source PRIVATE rtlsdr "C:/Users/TOSHIBA/Desktop/rtlsdrlib/rtlsdr/build/src/rtlsdr.lib"
    "C:/Users/TOSHIBA/Desktop/rtlsdrlib/rtlsdr/build/src/rtlsdr_static.lib") 
    
elseif (ANDROID)
    target_link_libraries(new_rtlsdr_source PUBLIC
        /sdr-kit/${ANDROID_ABI}/lib/libusb1.0.so
        /sdr-kit/${ANDROID_ABI}/lib/librtlsdr.so
    )
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBRTLSDR REQUIRED librtlsdr)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

    target_include_directories(new_rtlsdr_source PRIVATE ${LIBRTLSDR_INCLUDE_DIRS} ${LIBUSB_INCLUDE_DIRS})
    target_link_directories(new_rtlsdr_source PRIVATE ${LIBRTLSDR_LIBRARY_DIRS} ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(new_rtlsdr_source PRIVATE ${LIBRTLSDR_LIBRARIES} ${LIBUSB_LIBRARIES})
endif ()
