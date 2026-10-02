# LAME 3.100（MP3 エンコーダー）

ShareDAW の MP3 書き出しに使う。公式の配布物 `lame-3.100.tar.gz`
（https://sourceforge.net/projects/lame/files/lame/3.100/ 、SHA-256 `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`）
から、エンコーダー本体（`libmp3lame/` の C ソースとヘッダ）と `include/lame.h`、ライセンス（`COPYING` = LGPL 2）だけをそのまま取り出したもの。
変更はしていない。ビルドの設定は `cmake/Lame.cmake` と `cmake/lame/config.h`。
