#include <fstream>
#include <math.h>
#include <time.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json; 

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  //Define the current speed of the car
  double current_car_speed = 0.0;
  //lane variable drives the logic of changing the lanes
  int lane = 1;
  std::chrono::steady_clock::time_point lane_changed = std::chrono::steady_clock::now();

  h.onMessage([&current_car_speed, &map_waypoints_x, &map_waypoints_y, &map_waypoints_s, &map_waypoints_dx, &map_waypoints_dy, &lane, &lane_changed](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
	double const MAX_SPEED = 49.70;
	double const DIST_TOO_CLOSE_BREAK = 30; //30 or 40 meters
	double const DIST_TOO_CLOSE_CHANGE_PATH = 50;
	double const DIST_TO_FRONT_CAR = 25;
	double const DIST_TO_BACK_CAR = 15;
		
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

			// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
			/*
			double dist_inc = 0.3;  // controls speed limit
			for (int i = 0; i < 50; i++)
			{
				double next_s = car_s + (i + 1)* dist_inc;
				double next_d = 6;
				vector<double> xy = getXY(next_s, next_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);

				next_x_vals.push_back(xy[0]);  //car_x +(dist_inc*i)*cos(deg2rad(car_yaw))
				next_y_vals.push_back(xy[1]);  //car_y + (dist_inc*i)*sin(deg2rad(car_yaw))
			}
			*/
			//New Logic

			int prev_size = previous_path_x.size(); // capture the size of a previous path

			if (prev_size > 0)
			{
				car_s = end_path_s;
			}
			bool accident_possible = false;

			for (int i = 0; i < sensor_fusion.size(); i++)
			{
				float d = sensor_fusion[i][6];  // Gives the lane of the car "i". "i" represents the cars on the same side of the road
				if (d < (2 + 4 * lane + 2) && d >(2 + 4 * lane - 2)) // Each lane is 4m wide. So, if car is in lane 1, lane width is from 4m to 8m
				{
					// If other car is in the same lane as of our car then check the speed of the other car
					double vx = sensor_fusion[i][3];
					double vy = sensor_fusion[i][4];
					double other_car_velocity = sqrt(vx * vx + vy * vy);
					double other_car_s = sensor_fusion[i][5];  // s value of the other car

					other_car_s += ((double) prev_size * 0.02 * other_car_velocity);  // Find the car's future s value, 0.02 seconds 

					if ((other_car_s > car_s) && ((other_car_s - car_s) < DIST_TOO_CLOSE_CHANGE_PATH) && std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lane_changed).count() > 5000000)  // If Other car's future s value is greater than our car's future s value and distance between them is less than 30m then take action
					{
						// Define the logic for change of lane:
						//std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lane_changed).count() << std::endl;
						if (lane == 0) // left lane
						{
							double max_front_dist = 9999; //Max distance
							double max_back_dist = 9999; //Max distance
							for (int j = 0; j < sensor_fusion.size(); j++)
							{
								float dist_of_other_car = sensor_fusion[j][6];
								if (dist_of_other_car < (2 + 4 * 1 + 2) && dist_of_other_car >(2 + 4 * 1 - 2))  // if other cars in center lane
								{
									double vx_other = sensor_fusion[j][3];
									double vy_other = sensor_fusion[j][4];
									double check_speed_other = sqrt(vx_other * vx_other + vy_other * vy_other);
									double check_car_s_other = sensor_fusion[j][5];  // s value of the other car
									check_car_s_other += ((double)prev_size * 0.02 * check_speed_other);
									
									if ((check_car_s_other > car_s) && ((check_car_s_other - car_s) > DIST_TO_FRONT_CAR))
									{
										if ((check_car_s_other - car_s) < max_front_dist)
										{
											max_front_dist = check_car_s_other - car_s;
										}
									}
									else if ((check_car_s_other < car_s) && ((car_s - check_car_s_other) > DIST_TO_BACK_CAR))
									{
										if ((car_s - check_car_s_other) < max_back_dist)
										{
											max_back_dist = car_s - check_car_s_other;
										}
									}
									else // when other car is very near
									{
										max_front_dist = 0;
										max_back_dist = 0;
									}
								}
							}
							if (max_front_dist == 9999 && max_back_dist == 9999)
							{
								lane = 1; // if there is no car in center lane then turn to left
								lane_changed = std::chrono::steady_clock::now();
							}
							else if (max_front_dist == 0 && max_back_dist == 0)
							{
								lane = 0; // if there cars very near in center lane then do not turn
							}
							else
							{
								lane = 1; // if other cars in center lane are at safe distance
								lane_changed = std::chrono::steady_clock::now();
							}
						}
						else if (lane == 2) // Right lane
						{
							double max_front_dist = 9999; //Max distance
							double max_back_dist = 9999; //Max distance
							for (int j = 0; j < sensor_fusion.size(); j++)
							{
								float dist_of_other_car = sensor_fusion[j][6];
								if (dist_of_other_car < (2 + 4 * 1 + 2) && dist_of_other_car >(2 + 4 * 1 - 2)) // if other cars in center lane
								{
									double vx_other = sensor_fusion[j][3];
									double vy_other = sensor_fusion[j][4];
									double check_speed_other = sqrt(vx_other * vx_other + vy_other * vy_other);
									double check_car_s_other = sensor_fusion[j][5];  // s value of the other car
									check_car_s_other += ((double)prev_size * 0.02 * check_speed_other);

									if ((check_car_s_other > car_s) && ((check_car_s_other - car_s) > DIST_TO_FRONT_CAR))
									{
										if ((check_car_s_other - car_s) < max_front_dist)
										{
											max_front_dist = check_car_s_other - car_s;
										}
									}
									else if ((check_car_s_other < car_s) && ((car_s - check_car_s_other) > DIST_TO_BACK_CAR))
									{
										if ((car_s - check_car_s_other) < max_back_dist)
										{
											max_back_dist = car_s - check_car_s_other;
										}
									}
									else // when other car is very near
									{
										max_front_dist = 0;
										max_back_dist = 0;
									}
								}
							}
							if (max_front_dist == 9999 && max_back_dist == 9999)
							{
								lane = 1; // if there is no car in center lane then turn to left
								lane_changed = std::chrono::steady_clock::now();
							}
							else if (max_front_dist == 0 && max_back_dist == 0)
							{
								lane = 2; // if there cars very near in center lane then do not turn
							}
							else
							{
								lane = 1; // if other cars in center lane are at safe distance
								lane_changed = std::chrono::steady_clock::now();
							}
						}
						else if (lane == 1) // Center lane
						{
							double max_front_dist_left = 9999; //Max distance
							double max_front_dist_right = 9999; //Max distance
							double max_back_dist_left = 9999; //Max distance
							double max_back_dist_right = 9999; //Max distance
							for (int j = 0; j < sensor_fusion.size(); j++)
							{
								float dist_of_other_car = sensor_fusion[j][6];
								if (dist_of_other_car < (2 + 4 * 0 + 2) && dist_of_other_car >(2 + 4 * 0 - 2)) // left lane
								{
									double vx_other = sensor_fusion[j][3];
									double vy_other = sensor_fusion[j][4];
									double check_speed_other = sqrt(vx_other * vx_other + vy_other * vy_other);
									double check_car_s_other = sensor_fusion[j][5];  // s value of the other car
									check_car_s_other += ((double)prev_size * 0.02 * check_speed_other);

									if ((check_car_s_other > car_s) && ((check_car_s_other - car_s) > DIST_TO_FRONT_CAR))
									{
										if ((check_car_s_other - car_s) < max_front_dist_left)
										{ 
											max_front_dist_left = check_car_s_other - car_s;
										}
									}
									else if ((check_car_s_other < car_s) && ((car_s - check_car_s_other) > DIST_TO_BACK_CAR))
									{
										if ((car_s - check_car_s_other) < max_back_dist_left)
										{
											max_back_dist_left = car_s - check_car_s_other;
										}
									}
									else  // when other car is very near
									{
										max_front_dist_left = 0;
										max_back_dist_left = 0; 
									}
								}
								else if (dist_of_other_car < (2 + 4 * 2 + 2) && dist_of_other_car >(2 + 4 * 2 - 2))  // right lane
								{
									double vx_other = sensor_fusion[j][3];
									double vy_other = sensor_fusion[j][4];
									double check_speed_other = sqrt(vx_other * vx_other + vy_other * vy_other);
									double check_car_s_other = sensor_fusion[j][5];  // s value of the other car
									check_car_s_other += ((double)prev_size * 0.02 * check_speed_other);

									if ((check_car_s_other > car_s) && ((check_car_s_other - car_s) > DIST_TO_FRONT_CAR))
									{
										if ((check_car_s_other - car_s) < max_front_dist_right)
										{
											max_front_dist_right = check_car_s_other - car_s;
										}
									}
									else if ((check_car_s_other < car_s) && ((car_s - check_car_s_other) > DIST_TO_BACK_CAR))
									{
										if ((car_s - check_car_s_other) < max_back_dist_right)
										{
											max_back_dist_right = car_s - check_car_s_other;
										}
									}
									else  // when other car is very near
									{
										max_front_dist_right = 0;
										max_back_dist_right = 0;
									}
								}
							}
							if (max_front_dist_left == 9999 && max_back_dist_left == 9999 && max_front_dist_right == 9999 && max_back_dist_right == 9999)
							{
								lane = 0; // if there is no car in left and right lane then turn to left
								lane_changed = std::chrono::steady_clock::now();
							}
							else if (max_front_dist_left == 0 && max_back_dist_left == 0 && max_front_dist_right == 0 && max_back_dist_right == 0)
							{
								lane = 1; // if there cars very near in left and right lane then do not turn
							}
							else if (max_front_dist_left == 9999 && max_back_dist_left == 9999)
							{
								lane = 0; // if there is no car in left
								lane_changed = std::chrono::steady_clock::now();
							}
							else if (max_front_dist_right == 9999 && max_back_dist_right == 9999)
							{
								lane = 2; // if there is no car in right
								lane_changed = std::chrono::steady_clock::now();
							}
							else
							{
								if (max_front_dist_left+ max_back_dist_left > max_front_dist_right+ max_back_dist_right)
								{
									lane = 0; // More space on left side
									lane_changed = std::chrono::steady_clock::now();
								}
								else
								{
									lane = 2; // More space on right side
									lane_changed = std::chrono::steady_clock::now();
								}
							}
						}
					}	
					if ((other_car_s > car_s) && ((other_car_s - car_s) < DIST_TOO_CLOSE_BREAK))  // If Other car's future s value is greater than our car's future s value and distance between them is less than 30m then take action
					{
						accident_possible = true; // flag to reduce the speed and possibly change the lanes 
					}
				}
			}

			if (accident_possible)
			{
				current_car_speed = current_car_speed - 0.224; // 0.5 miles/hour is 0.224 meter/second 
			}
			else if (current_car_speed < MAX_SPEED)
			{
				current_car_speed = current_car_speed + 0.224; // 0.5 miles/hour is 0.224 meter/second 
			}

			//create a list of widely spaced (x,y) waypoints, evenly spaced at 30m, these waypoints are interpolated with Spline
			vector<double> ptsx;
			vector<double> ptsy;

			//Get the starting point or previous path end points of a car
			double source_x = car_x;
			double source_y = car_y;
			double source_yaw = deg2rad(car_yaw);

			if (prev_size < 2)  // If prev size is almost empty, use car as starting reference
			{
				//Use points that make the path tangent to the Car, makes calculations easy
				double prev_car_x = car_x - cos(car_yaw);
				double prev_car_y = car_y - sin(car_yaw);

				ptsx.push_back(prev_car_x);
				ptsx.push_back(car_x);

				ptsy.push_back(prev_car_y);
				ptsy.push_back(car_y);

			}
			else  // use the prev path's endpoints as starting reference
			{

				source_x = previous_path_x[prev_size - 1];
				source_y = previous_path_y[prev_size - 1];

				double source_x_prev = previous_path_x[prev_size - 2];
				double source_y_prev = previous_path_y[prev_size - 2];
				source_yaw = atan2(source_y - source_y_prev, source_x - source_x_prev);

				//Use points that make the path tangent to the previous path's end points
				ptsx.push_back(source_x_prev);
				ptsx.push_back(source_x);

				ptsy.push_back(source_y_prev);
				ptsy.push_back(source_y);
			}
			 
			//In Frenet, add 30m spaced points ahead of starting reference
			vector<double> next_waypoint0 = getXY(car_s + 30, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
			vector<double> next_waypoint1 = getXY(car_s + 60, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
			vector<double> next_waypoint2 = getXY(car_s + 90, (2 + 4 * lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

			ptsx.push_back(next_waypoint0[0]);
			ptsx.push_back(next_waypoint1[0]);
			ptsx.push_back(next_waypoint2[0]);

			ptsy.push_back(next_waypoint0[1]);
			ptsy.push_back(next_waypoint1[1]);
			ptsy.push_back(next_waypoint2[1]);

			for (int i = 0; i < ptsx.size(); i++)
			{
				//Chnage the coordinate system, shift car reference angle to 0 degrees
				double shift_x = ptsx[i] - source_x;
				double shift_y = ptsy[i] - source_y;

				//Apply Rotation
				ptsx[i] = (shift_x * cos(0 - source_yaw) - shift_y  * sin(0 - source_yaw));
				ptsy[i] = (shift_x * sin(0 - source_yaw) + shift_y  * cos(0 - source_yaw));
			}

			//create a spline
			tk::spline s;
			s.set_points(ptsx,ptsy);   // anchor points / Far spaced waypoints

			//define the points to be used for planner
			vector<double> next_x_vals;
			vector<double> next_y_vals;

			//start with all of the previous path points
			for (int i = 0; i < previous_path_x.size(); i++)
			{
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
			}

			//calculate how to break up spline points to travel at desired velocity
			double target_x = 30.0;  // define total distance d
			double target_y = s(target_x);  // spline function gives y for each x value
			double target_dist = sqrt(target_x * target_x + target_y * target_y);
			double x_addition = 0;  // increment x along the spline distance

			//fill up rest of the path planner after filling it with previou points, always 50 points below
			for (int i = 1; i <= 50- previous_path_x.size(); i++)
			{
				double N = target_dist / (0.02 * current_car_speed / 2.24);  // distance = N * 0.02 * Velocity, 5 miles per hour is 2.24 meter/second
				double x_point = x_addition + target_x / N;
				double y_point = s(x_point);
				x_addition = x_point;

				double x_ref = x_point;
				double y_ref = y_point;

				//rotate back to normal after rotating it earlier, change coordinate system to global coordinates
				x_point = x_ref * cos(source_yaw) - y_ref * sin(source_yaw);
				y_point = x_ref * sin(source_yaw) + y_ref * cos(source_yaw);

				x_point += source_x;
				y_point += source_y;

				next_x_vals.push_back(x_point);
				next_y_vals.push_back(y_point);
			}
			
			//New Logic - End
			
			json msgJson;
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}