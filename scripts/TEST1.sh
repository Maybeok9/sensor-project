# Controller code
sensor_ctl stats all
# Stats all too fast will make the buffer empty potentially lead to collector crash
sensor_ctl reset all

sensor_ctl pause 1
sensor_ctl resume 1
# Pause/resume would only show its working through log reading (further checking is needed)

# Set-srate capped at range 1 to 10
sensor_ctl set-srate all 4
sensor_ctl set-srate 0 9

# Inside controller there is no code for parsing the arg for log path and socket path yet
