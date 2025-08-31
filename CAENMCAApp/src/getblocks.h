
struct SEBLOCK
{
	time_t ref_time;
	std::vector<std::string> setpoint_value; ///< per period
	std::vector<float> fsetpoint_value; ///< per period
	std::vector<float> fsetpoint_spread; ///< per period
	std::string vi_name;
	std::string read_control;
	std::string set_control;
	std::string button_control;
	unsigned options;
	std::string nexus_name;
	float low_limit;
	float high_limit;
	std::string units;
	std::vector<std::string> current_value;  ///< per period
	std::vector<float> fcurrent_value;  ///< per period
	std::vector<float> fcurrent_spread;  ///< per period 
	bool is_real;
	bool is_array;
	std::vector<int> severity_id;
	std::vector<int> status_id;

	std::vector<float> time;
	std::vector<float> fvalues;
	std::vector< std::vector<float> > fvalues_array;
	std::vector<std::string> svalues;
	SEBLOCK() : ref_time(0), is_real(false), is_array(false), options(0), nexus_name(""), low_limit(0.0), high_limit(0.0) {}
};

typedef std::map<std::string, SEBLOCK> seblock_map_t;

epicsShareExtern void getBlocks(time_t start_time, seblock_map_t& blocks);
