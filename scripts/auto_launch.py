#!/usr/bin/env python

import argparse
import math
import os
import random
import subprocess
import sys

import rospkg
import yaml

if __name__ == '__main__':
	# Filter out any special ROS remapping arguments.
	# This is necessary if the script is being run from a ROS launch file.
	import rospy
	args = rospy.myargv(sys.argv)

	print ('start')

	rospy.init_node('robot_team_spawner')

	rospack = rospkg.RosPack()
	try:
		f = open(rospack.get_path('mav_tunnel_nav') + '/config/robot_settings/robots.yaml', 'r')
		ver = [float(x) for x in yaml.__version__.split('.')]
		if ver[0] >= 5 and ver[1] >= 1:
			dict_robot = yaml.load(f.read(), Loader=yaml.FullLoader)
		else:
			dict_robot = yaml.load(f.read())
	except Exception as e:
		print(e)

	cmd_list = {}
	for robot in dict_robot['robots']:
		cmd = [
			'roslaunch',
			'mav_tunnel_nav',
			'spawn_robot.launch',
			'mav_name:=' + robot,
			'x:=' + str(eval(str(dict_robot[robot]['x']))),
			'y:=' + str(eval(str(dict_robot[robot]['y']))),
			'z:=' + str(eval(str(dict_robot[robot]['z']))),
			'Y:=' + str(eval(str(dict_robot[robot]['Y']))),
			'map_only:=' + str(dict_robot[robot]['map_only']),
			'map_filename:=' + str(dict_robot[robot]['map_filename']),
			'output:=' + str(dict_robot[robot]['output']),
			'auto_pilot_type:=' + str(dict_robot['auto_pilot_type'])
		]
		if 'save_traj' in dict_robot[robot] and dict_robot[robot]['save_traj']:
			cmd += ['save_traj:=true']
		cmd_list[robot] = cmd

	proc_list = {}
	try:
		for robot in dict_robot['robots']:
			cmd = cmd_list[robot]
			print('Running command: ' + ' '.join(cmd))
			p = subprocess.Popen(cmd)
			proc_list[robot] = p
		for p in proc_list:
			proc_list[p].wait()
	except KeyboardInterrupt:
		pass
	finally:
		for p in proc_list:
			proc_list[p].wait()
