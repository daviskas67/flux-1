# storm.flux
# "Turing storm": waves crossing the grid like ants.
# Every hop toggles the accumulator of the cell it leaves -> a "memory trail".
# The trail renders as 'A' when shown with --acc.
steps 60
pause 70

# three independent wave sources
inject 0 2 E 1 0
inject 4 0 S 1 0
inject 15 7 W 1 0

# memory first (not consuming), then move
rule bit1 toggleacc
rule bit1 send E 2
