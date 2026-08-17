# wave.flux
# A single wave injected at top-left; it spreads East then South.
# The wave is a "1" that travels across the matrix like an ant.
steps 48
pause 120

# corner injects a 1 toward East with delay 0 (fires immediately)
inject 0 0 E 1 0

# rightward propagation with a hop delay of 2 ticks
rule bit1 send E 2

# bottom row redirects South so the wave falls down the right edge
cell 0 15 : bit1 send S 2
cell 1 15 : bit1 send S 2
cell 2 15 : bit1 send S 2
cell 3 15 : bit1 send S 2
cell 4 15 : bit1 send S 2
cell 5 15 : bit1 send S 2
cell 6 15 : bit1 send S 2
cell 7 15 : bit1 send S 2
cell 8 15 : bit1 send S 2
cell 9 15 : bit1 send S 2
cell 10 15 : bit1 send S 2
cell 11 15 : bit1 send S 2
cell 12 15 : bit1 send S 2
cell 13 15 : bit1 send S 2
cell 14 15 : bit1 send S 2
cell 15 15 : bit1 send S 2

# right column sends the wave back to the West along the bottom
cell 15 14 : bit1 send W 2
cell 15 13 : bit1 send W 2
cell 15 12 : bit1 send W 2
cell 15 11 : bit1 send W 2
cell 15 10 : bit1 send W 2
cell 15 9  : bit1 send W 2
cell 15 8  : bit1 send W 2
cell 15 7  : bit1 send W 2
cell 15 6  : bit1 send W 2
cell 15 5  : bit1 send W 2
cell 15 4  : bit1 send W 2
cell 15 3  : bit1 send W 2
cell 15 2  : bit1 send W 2
cell 15 1  : bit1 send W 2