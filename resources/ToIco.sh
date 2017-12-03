cat umbrella.svg > umbrella_temp.svg
convert -background transparent -density 256x256 -define icon:auto-resize -trim umbrella_temp.svg umbrella_green.ico

cat umbrella.svg       |      \
sed 's/88C057/C03758/' |      \
sed 's/75A64B/962B35/' > umbrella_temp.svg

convert -background transparent -density 256x256 -define icon:auto-resize -trim umbrella_temp.svg umbrella_red.ico

cat umbrella.svg       |      \
sed 's/88C057/C0C050/' |      \
sed 's/75A64B/A0A030/' > umbrella_temp.svg

convert -background transparent -density 256x256 -define icon:auto-resize -trim umbrella_temp.svg umbrella_yellow.ico


rm umbrella_temp.svg
