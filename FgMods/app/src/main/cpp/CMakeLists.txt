# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name.
project("fgaccess")

# --- OPENSSL CONFIGURATION ---
# Define the path to the OpenSSL directory
set(OPENSSL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openssl)

# Tell CMake where to find the OpenSSL headers
include_directories(${OPENSSL_DIR}/include)

# Import the OpenSSL Crypto static library (Notice the /lib/ added here)
add_library(crypto STATIC IMPORTED)
set_target_properties(crypto PROPERTIES 
    IMPORTED_LOCATION ${OPENSSL_DIR}/${ANDROID_ABI}/lib/libcrypto.a)
    
# Import the OpenSSL SSL static library (Notice the /lib/ added here)
add_library(ssl STATIC IMPORTED)
set_target_properties(ssl PROPERTIES 
    IMPORTED_LOCATION ${OPENSSL_DIR}/${ANDROID_ABI}/lib/libssl.a)
# -----------------------------

# Creates and names your shared library
add_library(
        fgaccess 
        SHARED
        native-lib.cpp)

# Specifies libraries CMake should link to your target library.
target_link_libraries(
        fgaccess
        crypto   
        ssl      
        android
        log)
