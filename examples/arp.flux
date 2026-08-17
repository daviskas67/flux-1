# Чип-арпеджио: волны-ноты бегут по трём рядам и включают частоты
# Каждый ряд = один канал. Волна проходит клетку => ставит частоту канала
# (нота). Волны стартуют по очереди с задержкой => последовательность.

rate 44100
sps 1470          # 30 тактов/сек — волна на клетку = ~одна нота 33 мс

# --- генератор волн, шлёт волну на восток по каждому ряду ---
cell 0 0 : bit0 emit E 0
cell 0 1 : bit0 emit E 0
cell 0 2 : bit0 emit E 0

# проводники вдоль рядов
cell 1 0 : bit1 send E 0
cell 1 1 : bit1 send E 0
cell 1 2 : bit1 send E 0

# --- ряды нот (частоты Гц) для канала 0 ---
cell 2 0 : bit1 freq 0 261.63   # C4
cell 4 0 : bit1 freq 0 329.63   # E4
cell 6 0 : bit1 freq 0 392.00   # G4
cell 8 0 : bit1 freq 0 523.25   # C5
cell 10 0 : bit1 freq 0 392.00  # G4
cell 12 0 : bit1 freq 0 329.63  # E4

# --- ряды нот для канала 1 ---
cell 2 1 : bit1 freq 1 261.63
cell 5 1 : bit1 freq 1 293.66   # D4
cell 8 1 : bit1 freq 1 349.23   # F4
cell 11 1 : bit1 freq 1 293.66

# --- ряды нот для канала 2 (бас, октавой ниже) ---
cell 2 2 : bit1 freq 2 130.81   # C3
cell 6 2 : bit1 freq 2 146.83   # D3
cell 10 2 : bit1 freq 2 174.61  # F3

# волны умирают на дальнем краю
cell 13 0 : bit1 stop
cell 13 1 : bit1 stop
cell 13 2 : bit1 stop

# --- каналы: тихие в начале, их включают волны ---
chan 0 square freq 0 amp 0.4
chan 1 square freq 0 amp 0.4
chan 2 square freq 0 amp 0.5

steps 200