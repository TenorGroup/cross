This copy of stb_image v2.30 comes from crosspoint-simulator commit ecc4784fc177f77bfcffb12b793b07282d29dbd4, src/stb_image.h. The file retains its MIT and public domain license text and is unchanged.

ProgressiveJpeg.cpp compiles only the JPEG decoder, with private symbols and PSRAM allocation hooks. X3 builds compile no stb implementation. ESP-IDF's bundled tjpgd and the pinned JPEGDEC provide the existing reduced progressive path; stb supplies full progressive AC decoding for PSRAM covers.
