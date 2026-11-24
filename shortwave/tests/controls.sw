# Power-on plays the first station. Manual tuning leaves scan, HOME recenters it.
master 3072
wav shortwave.wav
wait 12000
expect playing 1
expect station 0
expect dial 80
expect scan 1
expect noise 24
expect motion 50
expect peak_db_min -40
expect peak_db_max -0.5
shot first-station.png

spin SELECT 1
wait 150
expect dial 88
expect scan 0
tap HOME
wait 150
expect dial 80
spin PRESET 1
wait 250
expect station 1
expect dial 360
expect scan 0
shot second-station.png
wavstop

tap PLAY
wait 300
expect playing 0
peakreset
wait 600
expect peak_db_max -80
tap PLAY
wait 2500
expect playing 1
expect peak_db_min -40

master 0
wait 350
peakreset
wait 600
expect peak_db_max -80
