# Tuning and mix settings survive a power cycle. Playback always restarts.
spin PRESET 1
spin SELECT 1
spin K1 3
spin K2 -4
wait 150
expect station 1
expect dial 368
expect scan 0
expect noise 30
expect motion 42
tap PLAY
wait 150
expect playing 0
tap SAVE
wait 300
expect store_writes >0
power
wait 350
expect station 1
expect dial 368
expect scan 0
expect noise 30
expect motion 42
expect playing 1
shot after-power.png
