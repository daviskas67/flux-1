# pulse.flux
# A single generator cell emits a constant stream of 1-bits to the East.
# Every cell that receives a bit forwards it East. A "wave train".
steps 32
pause 100

# generator at top-left corner: always assert 1 toward East, delay 1
cell 0 0 : always emit E 1

# every cell passes its bit eastward (delay 1 => it hops every 2 ticks)
rule always send E 1