cmd_/home/god/workspace/kernel/modules.order := {   echo /home/god/workspace/kernel/sensor_driver.ko; :; } | awk '!x[$$0]++' - > /home/god/workspace/kernel/modules.order
