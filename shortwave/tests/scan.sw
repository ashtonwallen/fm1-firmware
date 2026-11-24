# Scan changes the dial without a press. Manual tuning or SEQ stops it.
expect scan 1
wait 50000
expect dial >80
shot scanning.png
spin SELECT 1
wait 150
expect scan 0
tap SEQ
wait 150
expect scan 1
tap SEQ
wait 150
expect scan 0
