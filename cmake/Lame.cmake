# MP3 の書き出しに使う LAME 3.100（LGPL 2）。公式のソースのエンコーダー部分（libmp3lame）を app/external/lame に同梱し、
# 静的ライブラリにする（ビルドのたびに外からダウンロードしない）。アセンブラ・SSE は使わない（どの OS・CPU でも同じ C のコード）
set(lame_SOURCE_DIR ${PROJECT_SOURCE_DIR}/app/external/lame)
set(_lame ${lame_SOURCE_DIR}/libmp3lame)
add_library(mp3lame STATIC
    ${_lame}/VbrTag.c ${_lame}/bitstream.c ${_lame}/encoder.c ${_lame}/fft.c ${_lame}/gain_analysis.c
    ${_lame}/id3tag.c ${_lame}/lame.c ${_lame}/mpglib_interface.c ${_lame}/newmdct.c ${_lame}/presets.c
    ${_lame}/psymodel.c ${_lame}/quantize.c ${_lame}/quantize_pvt.c ${_lame}/reservoir.c ${_lame}/set_get.c
    ${_lame}/tables.c ${_lame}/takehiro.c ${_lame}/util.c ${_lame}/vbrquantize.c ${_lame}/version.c)
target_include_directories(mp3lame PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lame ${_lame} PUBLIC ${lame_SOURCE_DIR}/include)
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
set_target_properties(mp3lame PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 他人のコードなので警告は出さない
if(MSVC)
    target_compile_options(mp3lame PRIVATE /W0)
else()
    target_compile_options(mp3lame PRIVATE -w)
endif()
