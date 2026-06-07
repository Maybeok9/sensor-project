#define SENSOR_MAGIC 'S'

/* Chọn sensor đang làm việc cho session này */
#define SENSOR_SELECT _IOW(SENSOR_MAGIC, 1, int)

/* Đọc một sample từ sensor đang được chọn */
#define SENSOR_READ_SAMPLE _IOR(SENSOR_MAGIC, 2, struct sensor_sample)

/* Set sampling rate cho sensor đang được chọn */
#define SENSOR_SET_RATE _IOW(SENSOR_MAGIC, 3, int)

/* Lấy stats của một sensor cụ thể (truyền sensor_id vào) */
#define SENSOR_GET_STATS _IOWR(SENSOR_MAGIC, 4, struct sensor_stats)

/* Reset counter của sensor đang được chọn */
#define SENSOR_RESET _IO(SENSOR_MAGIC, 5)

#define SENSOR_TYPE_TEMPERATURE 0
#define SENSOR_TYPE_HUMIDITY 1
#define SENSOR_TYPE_PRESSURE 2
#define SENSOR_COUNT 3
#define OUT_BUFFER_MAX 32

struct sensor_sample {
      int 	sensor_id; /* 0, 1, hoặc 2 */
      int32_t 	value; /* giá trị thực × 100 (fixed-point, 2 decimal places) */
      int64_t 	timestamp_us; /* ktime_get() tính bằng microseconds */
};
	
struct sensor_stats {
      int 	sensor_id; /* input: sensor nào cần query */
      uint32_t 	read_count;
      uint32_t 	error_count;
      uint32_t 	sampling_rate; /* Hz, default: 1 */
      int32_t 	last_value; /* giá trị sample gần nhất */
};
