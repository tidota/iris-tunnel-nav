// imu_odom.cpp
// 190604
// Odometry only by IMU
//
//
// How to calculate 3D pose from 3D accelerations.
// https://arxiv.org/pdf/1704.06053.pdf
//
// If it is necessary to calculate the orientations from angular velocities.
// https://folk.uio.no/jeanra/Informatics/QuaternionsAndIMUs.html
//

#include <cmath>

#include <string>
#include <mutex>

#include <geometry_msgs/Pose.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

std::string odom_reset_topic;
std::string imu_topic;
std::string odom_topic;
ros::Publisher odom_pub;
std::string odom_pose_topic;
ros::Publisher odom_pose_pub;
std::string world_frame_id;
std::string odom_frame_id;

double grav = 9.81;
double x, y, z;
double vx, vy, vz;
ros::Time last_time;
bool initial_imu = true;
bool calibration = true;

tf::Vector3 init_grav_dir;
int sample_num;

tf::Transform t;
std::mutex t_mutex;

std::mutex odom_reset_mutex;

tf::Transform init_dir;

////////////////////////////////////////////////////////////////////////////////
void locCallback(const nav_msgs::Odometry::ConstPtr& locdata)
{
  if (!initial_imu && !calibration)
  {
    std::lock_guard<std::mutex> lk(odom_reset_mutex);
    x = locdata->pose.pose.position.x;
    y = locdata->pose.pose.position.y;
    z = locdata->pose.pose.position.z;
    vx = locdata->twist.twist.linear.x;
    vy = locdata->twist.twist.linear.y;
    vz = locdata->twist.twist.linear.z;
  }
}

////////////////////////////////////////////////////////////////////////////////
void imuCallback(const sensor_msgs::Imu::ConstPtr& imu)
{
  ros::Time cur_time = ros::Time::now();

  if (initial_imu)
  {
    init_grav_dir = tf::Vector3(
                      imu->linear_acceleration.x,
                      imu->linear_acceleration.y,
                      imu->linear_acceleration.z);
    sample_num = 1;
    last_time = cur_time;
    initial_imu = false;
    return;
  }

  double dt = (cur_time - last_time).toSec();

  if (calibration)
  {
    init_grav_dir += tf::Vector3(
                      imu->linear_acceleration.x,
                      imu->linear_acceleration.y,
                      imu->linear_acceleration.z);
    sample_num++;
    if (dt > 10.0 && sample_num >= 100)
    {
      init_grav_dir /= sample_num;
      grav = init_grav_dir.length();

      // double roll, pitch, yaw;
      // tf::Matrix3x3 mat(q);
      // mat.getRPY(roll, pitch, yaw);
      // ROS_INFO_STREAM("roll: " << (roll/M_PI*180) << ", pitch: " << (pitch/M_PI*180) << ", yaw: " << (yaw/M_PI*180));

      last_time = cur_time;
      calibration = false;
    }
    return;
  }

  last_time = cur_time;

  // acceleration
  tf::Vector3 acc_vec(
    imu->linear_acceleration.x, imu->linear_acceleration.y, imu->linear_acceleration.z
  );
  tf::Quaternion imu_orientation(imu->orientation.x, imu->orientation.y, imu->orientation.z, imu->orientation.w);
  tf::Quaternion global_ori = init_dir.getRotation() * imu_orientation;
  global_ori.normalize();

  tf::Transform transform(global_ori, tf::Vector3(0,0,0));
  acc_vec = transform * acc_vec;
  double ax = acc_vec.x();
  double ay = acc_vec.y();
  double az = acc_vec.z() - grav;

  {
    std::lock_guard<std::mutex> lk(odom_reset_mutex);

    // velocities
    x += vx * dt;
    y += vy * dt;
    z += vz * dt;

    //if (std::fabs(ax) >= 1.0)
      vx += ax * dt;
    //if (std::fabs(ay) >= 1.0)
      vy += ay * dt;
    //if (std::fabs(az) >= 1.0)
      vz += az * dt;

    // if (vx > 1.0)
    //   vx = 1.0;
    // else if (vx < -1.0)
    //   vx = -1.0;
    // if (vy > 1.0)
    //   vy = 1.0;
    // else if (vy < -1.0)
    //   vy = -1.0;
    // if (vz > 1.0)
    //   vz = 1.0;
    // else if (vz < -1.0)
    //   vz = -1.0;

  //ROS_INFO_STREAM("dt: " << dt << ", ax: " << ax << ", ay: " << ay << ", az: " << az);

    nav_msgs::Odometry odom;
    odom.header = imu->header;
    odom.header.frame_id = world_frame_id;
    odom.child_frame_id = odom_frame_id;
    odom.pose.pose.position.x = x;
    odom.pose.pose.position.y = y;
    odom.pose.pose.position.z = z;
    odom.pose.pose.orientation.x = global_ori.x();
    odom.pose.pose.orientation.y = global_ori.y();
    odom.pose.pose.orientation.z = global_ori.z();
    odom.pose.pose.orientation.w = global_ori.w();
    odom.twist.twist.linear.x = vx;
    odom.twist.twist.linear.y = vy;
    odom.twist.twist.linear.z = vz;
    odom.twist.twist.angular = imu->angular_velocity;

    odom_pub.publish(odom);

    geometry_msgs::Pose odom_pose;
    odom_pose.position = odom.pose.pose.position;
    odom_pose.orientation = odom.pose.pose.orientation;

    odom_pose_pub.publish(odom_pose);

    std::lock_guard<std::mutex> lk2(t_mutex);
    t = tf::Transform(global_ori, tf::Vector3(x, y, z));
  }
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
  ros::init(argc, argv, "imu_odom");
  ros::NodeHandle nh;
  ros::NodeHandle pnh("~");
  tf::TransformBroadcaster tf_broadcaster;

  double init_Y;
  pnh.getParam("init_Y", init_Y);
  tf::Quaternion init_rot;
  init_rot.setRPY(0, 0, init_Y);
  init_dir = tf::Transform(init_rot, tf::Vector3(0, 0, 0));

  t = init_dir;

  x = y = z = 0;
  vx = vy = vz = 0;
  last_time = ros::Time::now();

  pnh.getParam("odom_reset_topic", odom_reset_topic);
  pnh.getParam("imu_topic", imu_topic);
  pnh.getParam("odom_topic", odom_topic);
  pnh.getParam("odom_pose_topic", odom_pose_topic);
  pnh.getParam("world_frame_id", world_frame_id);
  pnh.getParam("odom_frame_id", odom_frame_id);

  odom_pub = nh.advertise<nav_msgs::Odometry>(odom_topic, 1000);
  odom_pose_pub = nh.advertise<geometry_msgs::Pose>(odom_pose_topic, 1000);
  ros::Subscriber imu_sub = nh.subscribe(imu_topic, 1000, imuCallback);
  ros::Subscriber odom_reset_sub
    = nh.subscribe(odom_reset_topic, 1000, locCallback);

  ros::Time checkpoint = ros::Time::now();
  const ros::Duration duration(0.01);
  while(ros::ok())
  {
    if (ros::Time::now() - checkpoint >= duration)
    {
      std::lock_guard<std::mutex> lk(t_mutex);
      checkpoint = ros::Time::now();
      tf::StampedTransform tf_stamped(t, checkpoint, world_frame_id, odom_frame_id);
      tf_broadcaster.sendTransform(tf_stamped);
    }

    ros::spinOnce();
  }

  return(0);
}
