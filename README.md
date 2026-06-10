1.Build and Run

// Project structure

The project require the 3 sensors (generator) to random gen the next data then put them in their own ring buffer (s_wait).

Then with those data in s_wait the userspace daemon (collector) will proceed to read off of them via its 3 reader thread (r_thread) to put them in the collector own ring buffer (c_hold).

Inside the collector there is 1 writer thread (w_thread) which has the duty of taking data from c_hold to output them into log file.

The final thread inside the daemon is the IPC listener (listener) which utilize the Unix Domain Socket system to bring up a link to a a socket file (socket_p) awaiting potential connection from other thread or process.

When the user use the registered cli_tool (s_ctl), it will connect to socket_p and will act as a provider which send according command to the listener which in turn will call ioctl() or change settings of r_thread.
	
	// Usage
	Run 
	```code
	cd /kernel &&
	make clean &&
	make &&
	sudo rmmod sensor_driver.ko 2>/dev/null || true && 
	sudo insmod sensor_driver.ko &&
	cd ../userspace &&
	gcc sensor_collector.c -o sensor_collector -lpthread &&
	gcc sensor_ctl.c -o sensor_ctl &&
	sudo cp sensor_collector /usr/bin &&
	sudo cp sensor_ctl /usr/bin &&
	cd ..
	```
Now use cmd "sudo sensor_collector" to use default parameter or change default params with :
	
	sensor_collector [OPTIONS]
	
		-d <device>	Device path (default: /dev/sensor0)
	
		-l <logfile>	Log file path (default: /tmp/sensor_collector.log)
	
		-s <sockpath>	Unix socket path (default: /tmp/run.socket)
	
		-i <ms>		Initial interval cho tất cả sensor (default: 1000ms)
		
		-i0/i1/i2 <ms>	Initial interval riêng từng sensor (override -i)
		
		-v		Verbose mode
	
	
Upon running sensor_collector, you can then use a new terminal to run :
	
	sensor_ctl 
	
		stats <0|1|2|all>		Show stats of each sensor
	
		set-rate <0|1|2|all> <value>	Set read speed of each r_thread
	
		pause <0|1|2>			Pause a r_thread
	
		resume <0|1|2>			Resume a paused r_thread
	
		status 				Return status of all sensors
	
		reset <0|1|2|all>		Reset all sensor to their default values
	
		set-srate <0|1|2|all> <value>	Set sampling rate of sensor
	
	
2.Design decisions

Each sensor has 1 data generator and 1 data consumer, so each should have its own spinlock as the consumer 1 when run would only lock sensor 1 for its use. Had there been a global lock, once consumer 1 run it would block every other consumer and data generator, this would technically work but defeat the purpose of using r_thread. So for each region of shared resource, 1 spinlock/mutex is best of use.
	
Per file context utilize writing into a file descriptor private data region so kernel code can read it then map the according sensor device to the current thread, without it the file descriptor created in r_thread won't have a way to get to the sensor device instant.
	
Using a mutex lock for the c_hold buffer as 3 r_thread is writing into it and 1 w_thread to read it data onto a log file, reason why only have 1 lock because all 3 r_thread update the same resource (head,tail,...) but also have their own resource (error_count, read_count,...), so it is best to use one lock for all.
	
pause/resume use 3 pthread_con_t and 3 global flag for each r_thread when trigger, listener thread will take the global mutex lock (pr_lock) then update the flag accordingly, then inside the r_thread it check that the flag is changed then choose to pthread_cond_wait or continue its programming.
	
Reader need to join first to make sure no more data is coming into the c_hold buffer, so that the remaining data can be consume by the writer to log to file, else there will still be data un written inside c_hold.
	
3.Known limitations

"sensor_ctl stats all" haven't shown the desired board.

No filtering of input yet

Further testing is needed for stress testing
	
4.Testing

Future testing will be update
