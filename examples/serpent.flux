# serpent.flux
# A single bit drawn along a serpentine (snake) path.
# Cleaner than wave.flux: every cell has exactly one rule, no collisions.
steps 130
pause 90

inject 0 0 E 1 0

# rows 0..14: travel East, then South on the right edge
rule bit1 send E 2

cell 15 0 : bit1 send S 2
cell 15 1 : bit1 send W 2
cell 14 1 : bit1 send S 2
cell 14 2 : bit1 send E 2
cell 15 2 : bit1 send S 2
cell 15 3 : bit1 send W 2
cell 14 3 : bit1 send S 2
cell 14 4 : bit1 send E 2
cell 15 4 : bit1 send S 2
cell 15 5 : bit1 send W 2
cell 14 5 : bit1 send S 2
cell 14 6 : bit1 send E 2
cell 15 6 : bit1 send S 2
cell 15 7 : bit1 send W 2
cell 14 7 : bit1 send S 2
cell 14 8 : bit1 send E 2
cell 15 8 : bit1 send S 2
cell 15 9 : bit1 send W 2
cell 14 9 : bit1 send S 2
cell 14 10 : bit1 send E 2
cell 15 10 : bit1 send S 2
cell 15 11 : bit1 send W 2
cell 14 11 : bit1 send S 2
cell 14 12 : bit1 send E 2
cell 15 12 : bit1 send S 2
cell 15 13 : bit1 send W 2
cell 14 13 : bit1 send S 2
cell 14 14 : bit1 send E 2
cell 15 14 : bit1 send S 2