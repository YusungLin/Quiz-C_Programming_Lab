# gnuplot config script
reset
set term png
set output 'improved.png'
set title "title" font 'Verdana, 21'
set xlabel "pushed elements (x 100)"
set ylabel "time (ms)"

set xrange [0:10000]
set xtics 1000
set yrange [0: 250]
set ytics 25

set grid
plot "improved.dat" using 1:2 title "original" with lines
