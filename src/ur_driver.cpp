/*
 * ur_driver.cpp
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <thomas.timm.dk@gmail.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Thomas Timm Andersen
 * ----------------------------------------------------------------------------
 */

#include "ur_modern_driver/ur_driver.h"

UrDriver::UrDriver(std::condition_variable& rt_msg_cond,
		std::condition_variable& msg_cond, std::string host,
		unsigned int reverse_port, double servoj_time, unsigned int safety_count_max,
		double max_time_step, double min_payload, double max_payload) :
		REVERSE_PORT_(reverse_port), maximum_time_step_(max_time_step), minimum_payload_(
				min_payload), maximum_payload_(max_payload), servoj_time_(servoj_time) {
	char buffer[256];
	struct sockaddr_in serv_addr;
	int n, flag;
	//char *ip_addr;

	rt_interface_ = new UrRealtimeCommunication(rt_msg_cond, host,
			safety_count_max);
	new_sockfd_ = -1;
	sec_interface_ = new UrCommunication(msg_cond, host);

	incoming_sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (incoming_sockfd_ < 0) {
		print_fatal("ERROR opening socket for reverse communication");
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(REVERSE_PORT_);
	flag = 1;
	setsockopt(incoming_sockfd_, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
			sizeof(int));
	setsockopt(incoming_sockfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
	if (bind(incoming_sockfd_, (struct sockaddr *) &serv_addr,
			sizeof(serv_addr)) < 0) {
		print_fatal("ERROR on binding socket for reverse communication");
	}
	listen(incoming_sockfd_, 5);
}

std::vector<double> UrDriver::interp_cubic(double t, double T,
		std::vector<double> p0_pos, std::vector<double> p1_pos,
		std::vector<double> p0_vel, std::vector<double> p1_vel) {
	/*Returns positions of the joints at time 't' */
	std::vector<double> positions;
	for (unsigned int i = 0; i < p0_pos.size(); i++) {
		double a = p0_pos[i];
		double b = p0_vel[i];
		double c = (-3 * p0_pos[i] + 3 * p1_pos[i] - 2 * T * p0_vel[i]
				- T * p1_vel[i]) / pow(T, 2);
		double d = (2 * p0_pos[i] - 2 * p1_pos[i] + T * p0_vel[i]
				+ T * p1_vel[i]) / pow(T, 3);
		positions.push_back(a + b * t + c * pow(t, 2) + d * pow(t, 3));
	}
	return positions;
}
 /*
void UrDriver::addTraj(std::vector<double> inp_timestamps,
		std::vector<std::vector<double> > inp_positions,
		std::vector<std::vector<double> > inp_velocities) {
	// DEPRECATED
	printf("!! addTraj is deprecated !!\n");
	std::vector<double> timestamps;
	std::vector<std::vector<double> > positions;
	std::string command_string = "def traj():\n";

	for (unsigned int i = 1; i < inp_timestamps.size(); i++) {
		timestamps.push_back(inp_timestamps[i - 1]);
		double dt = inp_timestamps[i] - inp_timestamps[i - 1];
		unsigned int steps = (unsigned int) ceil(dt / maximum_time_step_);
		double step_size = dt / steps;
		for (unsigned int j = 1; j < steps; j++) {
			timestamps.push_back(inp_timestamps[i - 1] + step_size * j);
		}
	}
	// //make sure we come to a smooth stop
	// while (timestamps.back() < inp_timestamps.back()) {
	//   timestamps.push_back(timestamps.back() + 0.008);
	// }
	// timestamps.pop_back();

	unsigned int j = 0;
	for (unsigned int i = 0; i < timestamps.size(); i++) {
		while (inp_timestamps[j] <= timestamps[i]) {
			j += 1;
		}
		positions.push_back(
				UrDriver::interp_cubic(timestamps[i] - inp_timestamps[j - 1],
						inp_timestamps[j] - inp_timestamps[j - 1],
						inp_positions[j - 1], inp_positions[j],
						inp_velocities[j - 1], inp_velocities[j]));
	}

	timestamps.push_back(inp_timestamps[inp_timestamps.size() - 1]);
	positions.push_back(inp_positions[inp_positions.size() - 1]);
	/// This is actually faster than using a stringstream :-o
	for (unsigned int i = 1; i < timestamps.size(); i++) {
		char buf[128];
		sprintf(buf,
				"\tservoj([%1.5f, %1.5f, %1.5f, %1.5f, %1.5f, %1.5f], t=%1.5f)\n",
				positions[i][0], positions[i][1], positions[i][2],
				positions[i][3], positions[i][4], positions[i][5],
				timestamps[i] - timestamps[i - 1]);
		command_string += buf;
	}
	command_string += "end\n";
	//printf("%s", command_string.c_str());
	rt_interface_->addCommandToQueue(command_string);

}
*/
void UrDriver::doTraj(std::vector<double> inp_timestamps,
		std::vector<std::vector<double> > inp_positions,
		std::vector<std::vector<double> > inp_velocities) {

	std::chrono::high_resolution_clock::time_point t0, t;
	std::vector<double> positions;
	unsigned int j;

	UrDriver::uploadProg();

	t0 = std::chrono::high_resolution_clock::now();
	t = t0;
	j = 0;
	while (inp_timestamps[inp_timestamps.size() - 1]
			>= std::chrono::duration_cast<std::chrono::duration<double>>(t - t0).count()) {
		while (inp_timestamps[j]
				<= std::chrono::duration_cast<std::chrono::duration<double>>(
						t - t0).count() && j < inp_timestamps.size() - 1) {
			j += 1;
		}
		positions = UrDriver::interp_cubic(
				std::chrono::duration_cast<std::chrono::duration<double>>(
						t - t0).count() - inp_timestamps[j - 1],
				inp_timestamps[j] - inp_timestamps[j - 1], inp_positions[j - 1],
				inp_positions[j], inp_velocities[j - 1], inp_velocities[j]);

		UrDriver::servoj(positions);

		// oversample with 4 * sample_time
		std::this_thread::sleep_for(std::chrono::milliseconds((int) ((servoj_time_*1000)/4.)));
		t = std::chrono::high_resolution_clock::now();
	}
	//Signal robot to stop driverProg()
	UrDriver::closeServo(positions);
}

void UrDriver::servoj(std::vector<double> positions,
		int keepalive, double time) {
	unsigned int bytes_written;
	int tmp;
	unsigned char buf[32];
	if (time < 0.016) {
		time = servoj_time_;
	}
	for (int i = 0; i < 6; i++) {
		tmp = htonl((int) (positions[i] * MULT_JOINTSTATE_));
		buf[i * 4] = tmp & 0xff;
		buf[i * 4 + 1] = (tmp >> 8) & 0xff;
		buf[i * 4 + 2] = (tmp >> 16) & 0xff;
		buf[i * 4 + 3] = (tmp >> 24) & 0xff;
	}
	tmp = htonl((int) (time * MULT_TIME_));
	buf[6 * 4] = tmp & 0xff;
	buf[6 * 4 + 1] = (tmp >> 8) & 0xff;
	buf[6 * 4 + 2] = (tmp >> 16) & 0xff;
	buf[6 * 4 + 3] = (tmp >> 24) & 0xff;
	tmp = htonl((int) keepalive);
	buf[7 * 4] = tmp & 0xff;
	buf[7 * 4 + 1] = (tmp >> 8) & 0xff;
	buf[7 * 4 + 2] = (tmp >> 16) & 0xff;
	buf[7 * 4 + 3] = (tmp >> 24) & 0xff;
	bytes_written = write(new_sockfd_, buf, 32);
}

void UrDriver::stopTraj() {
	rt_interface_->addCommandToQueue("stopj(10)\n");
}

void UrDriver::uploadProg() {
	std::string cmd_str;
	char buf[128];
	cmd_str = "def driverProg():\n";

	sprintf(buf, "\tMULT_jointstate = %i\n", MULT_JOINTSTATE_);
	cmd_str += buf;

	sprintf(buf, "\tMULT_time = %i\n", MULT_TIME_);
	cmd_str += buf;

	cmd_str += "\tSERVO_IDLE = 0\n";
	cmd_str += "\tSERVO_RUNNING = 1\n";
	cmd_str += "\tcmd_servo_state = SERVO_IDLE\n";
	cmd_str += "\tcmd_servo_id = 0  # 0 = idle, -1 = stop\n";
	cmd_str += "\tcmd_servo_q = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]\n";
	cmd_str += "\tcmd_servo_dt = 0.0\n";
	cmd_str += "\tdef set_servo_setpoint(q, dt):\n";
	cmd_str += "\t\tenter_critical\n";
	cmd_str += "\t\tcmd_servo_state = SERVO_RUNNING\n";
	cmd_str += "\t\tcmd_servo_q = q\n";
	cmd_str += "\t\tcmd_servo_dt = dt\n";
	cmd_str += "\t\texit_critical\n";
	cmd_str += "\tend\n";
	cmd_str += "\tthread servoThread():\n";
	cmd_str += "\t\tstate = SERVO_IDLE\n";
	cmd_str += "\t\twhile True:\n";
	cmd_str += "\t\t\tenter_critical\n";
	cmd_str += "\t\t\tq = cmd_servo_q\n";
	cmd_str += "\t\t\tdt = cmd_servo_dt\n";
	cmd_str += "\t\t\tdo_brake = False\n";
	cmd_str += "\t\t\tif (state == SERVO_RUNNING) and ";
	cmd_str += "(cmd_servo_state == SERVO_IDLE):\n";
	cmd_str += "\t\t\t\tdo_brake = True\n";
	cmd_str += "\t\t\tend\n";
	cmd_str += "\t\t\tstate = cmd_servo_state\n";
	cmd_str += "\t\t\tcmd_servo_state = SERVO_IDLE\n";
	cmd_str += "\t\t\texit_critical\n";
	cmd_str += "\t\t\tif do_brake:\n";
	cmd_str += "\t\t\t\tstopj(1.0)\n";
	cmd_str += "\t\t\t\tsync()\n";
	cmd_str += "\t\t\telif state == SERVO_RUNNING:\n";
	cmd_str += "\t\t\t\tservoj(q, t=dt)\n";
	cmd_str += "\t\t\telse:\n";
	cmd_str += "\t\t\t\tsync()\n";
	cmd_str += "\t\t\tend\n";
	cmd_str += "\t\tend\n";
	cmd_str += "\tend\n";

	sprintf(buf, "\tsocket_open(\"%s\", %i)\n", ip_addr_.c_str(),
			REVERSE_PORT_);
	cmd_str += buf;

	cmd_str += "\tthread_servo = run servoThread()\n";
	cmd_str += "\tkeepalive = 1\n";
	cmd_str += "\twhile keepalive > 0:\n";
	cmd_str += "\t\tparams_mult = socket_read_binary_integer(6+1+1)\n";
	cmd_str += "\t\tif params_mult[0] > 0:\n";
	cmd_str += "\t\t\tq = [params_mult[1] / MULT_jointstate, ";
	cmd_str += "params_mult[2] / MULT_jointstate, ";
	cmd_str += "params_mult[3] / MULT_jointstate, ";
	cmd_str += "params_mult[4] / MULT_jointstate, ";
	cmd_str += "params_mult[5] / MULT_jointstate, ";
	cmd_str += "params_mult[6] / MULT_jointstate]\n";
	cmd_str += "\t\t\tt = params_mult[7] / MULT_time\n";
	cmd_str += "\t\t\tkeepalive = params_mult[8]\n";
	cmd_str += "\t\t\tset_servo_setpoint(q, t)\n";
	cmd_str += "\t\tend\n";
	cmd_str += "\tend\n";
	cmd_str += "\tsleep(.1)\n";
	cmd_str += "\tsocket_close()\n";
	cmd_str += "end\n";

	rt_interface_->addCommandToQueue(cmd_str);
	UrDriver::openServo();
}

void UrDriver::openServo() {
	struct sockaddr_in cli_addr;
	socklen_t clilen;
	clilen = sizeof(cli_addr);
	new_sockfd_ = accept(incoming_sockfd_, (struct sockaddr *) &cli_addr,
			&clilen);
	if (new_sockfd_ < 0) {
		print_fatal("ERROR on accepting reverse communication");
	}
}
void UrDriver::closeServo(std::vector<double> positions) {
	if (positions.size() != 6)
		UrDriver::servoj(rt_interface_->robot_state_->getQActual(), 0);
	else
		UrDriver::servoj(positions, 0);
	close(new_sockfd_);
}

bool UrDriver::start() {
	if (!sec_interface_->start())
		return false;
	rt_interface_->robot_state_->setVersion(
			sec_interface_->robot_state_->getVersion());
	if (!rt_interface_->start())
		return false;
	ip_addr_ = rt_interface_->getLocalIp(); //inet_ntoa(serv_addr.sin_addr);
	char buf[256];
	sprintf(buf, "Listening on %s:%u\n", ip_addr_.c_str(), REVERSE_PORT_);
	print_debug(buf);
	return true;

}

void UrDriver::halt() {
	sec_interface_->halt();
	rt_interface_->halt();
	close(incoming_sockfd_);
}

void UrDriver::setSpeed(double q0, double q1, double q2, double q3, double q4,
		double q5, double acc) {
	rt_interface_->setSpeed(q0, q1, q2, q3, q4, q5, acc);
}

std::vector<std::string> UrDriver::getJointNames() {
	return joint_names_;
}

void UrDriver::setJointNames(std::vector<std::string> jn) {
	joint_names_ = jn;
}

void UrDriver::setToolVoltage(unsigned int v) {
	char buf[256];
	sprintf(buf, "sec setOut():\n\tset_tool_voltage(%d)\nend\n", v);
	rt_interface_->addCommandToQueue(buf);
	print_debug(buf);
}
void UrDriver::setFlag(unsigned int n, bool b) {
	char buf[256];
	sprintf(buf, "sec setOut():\n\tset_flag(%d, %s)\nend\n", n,
			b ? "True" : "False");
	rt_interface_->addCommandToQueue(buf);
	print_debug(buf);
}
void UrDriver::setDigitalOut(unsigned int n, bool b) {
	char buf[256];
	sprintf(buf, "sec setOut():\n\tset_digital_out(%d, %s)\nend\n", n,
			b ? "True" : "False");
	rt_interface_->addCommandToQueue(buf);
	print_debug(buf);

}
void UrDriver::setAnalogOut(unsigned int n, double f) {
	char buf[256];
	sprintf(buf, "sec setOut():\n\tset_analog_out(%d, %1.4f)\nend\n", n, f);
	rt_interface_->addCommandToQueue(buf);
	print_debug(buf);
}

bool UrDriver::setPayload(double m) {
	if ((m < maximum_payload_) && (m > minimum_payload_)) {
		char buf[256];
		sprintf(buf, "sec setOut():\n\tset_payload(%1.3f)\nend\n", m);
		rt_interface_->addCommandToQueue(buf);
		print_debug(buf);
		return true;
	} else
		return false;
}

void UrDriver::setMinPayload(double m) {
	if (m > 0) {
		minimum_payload_ = m;
	} else {
		minimum_payload_ = 0;
	}

}
void UrDriver::setMaxPayload(double m) {
	maximum_payload_ = m;
}
void UrDriver::setServojTime(double t) {
	if (t > 0.016) {
		servoj_time_ = t;
	} else {
		servoj_time_ = 0.016;
	}
}
