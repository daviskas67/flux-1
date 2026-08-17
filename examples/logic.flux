# logic.flux
# 1-bit accumulator logic: an AND gate made from waves.
# Two streams meet; acc = bit1 AND bit2 (both must arrive to set acc).
steps 60
pause 120

inject 3 3 E 1 0
inject 3 5 E 1 0

# route both streams toward the gate at (8,4)
cell 3 3 : bit1 send E 1
cell 4 3 : bit1 send E 1
cell 5 3 : bit1 send E 1
cell 6 3 : bit1 send S 1
cell 6 4 : bit1 send E 1
cell 7 4 : bit1 send E 1

cell 3 5 : bit1 send E 1
cell 4 5 : bit1 send E 1
cell 5 5 : bit1 send E 1
cell 6 5 : bit1 send N 1
cell 6 4 : bit1 send E 1
cell 7 4 : bit1 send E 1

# gate: when a bit lands here, add to acc (acc counts arrivals, capped at 1)
cell 8 4 : bit1 setacc 1
cell 8 4 : bit1 stop