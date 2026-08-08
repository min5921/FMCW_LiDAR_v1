# FMCW LiDAR ROS1 Noetic / RViz workspace

`Ros_project` is a self-contained catkin workspace for the FMCW LiDAR UDP point
protocol v2. The implementation is C++14; no Python node is used in the receive
or publish path.

## Data path and coordinate contract

```text
FMCW LiDAR application
  -> little-endian FMCW UDP v2 segments
  -> udp_pointcloud_receiver_node (C++, bounded frame reassembly)
  -> /fmcw/points (sensor_msgs/PointCloud2)
  -> RViz
```

Every published point follows the ROS/RViz right-handed sensor convention:

- `x`: forward, metres
- `y`: left, metres
- `z`: up, metres
- `intensity`: FMCW peak intensity, `float32`
- `velocity`: calibrated radial velocity in m/s, `float32`

The receiver does not swap axes. The FMCW sender must already transmit this
coordinate meaning. Only complete raster frames are published.

## Target machine

- Ubuntu 20.04
- ROS1 Noetic
- C++14 compiler and catkin
- RViz (`ros-noetic-rviz`), or `ros-noetic-desktop-full`

Example dependency installation:

```bash
sudo apt update
sudo apt install build-essential ros-noetic-ros-base ros-noetic-rviz
```

## Copy and build on the ROS computer

Copy the complete `Ros_project` directory to the ROS computer. It can live at
any path, for example `~/FMCW_LiDAR/Ros_project`.

```bash
cd ~/FMCW_LiDAR/Ros_project
source /opt/ros/noetic/setup.bash
rosdep install --from-paths src --ignore-src -r -y
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

Run the protocol unit tests after the first build:

```bash
catkin_make run_tests_fmcw_lidar_rviz
catkin_test_results build/test_results
```

## Run receiver and RViz

```bash
source /opt/ros/noetic/setup.bash
source ~/FMCW_LiDAR/Ros_project/devel/setup.bash
roslaunch fmcw_lidar_rviz udp_rviz.launch
```

To accept packets only from the FMCW sender at `192.168.0.20`:

```bash
roslaunch fmcw_lidar_rviz udp_rviz.launch \
  sender_ip:=192.168.0.20 port:=9000
```

For a headless receiver:

```bash
roslaunch fmcw_lidar_rviz udp_rviz.launch rviz:=false
```

Set the main FMCW project to send to the ROS computer:

```yaml
udp:
  enabled: true
  target_ip: "<ROS_COMPUTER_IPV4>"
  target_port: 9000
  packet_format_version: 2
```

Allow inbound UDP port 9000 in the ROS computer firewall if it is enabled.

## C++ test sender

With the receiver running, open another terminal and send a synthetic surface:

```bash
source /opt/ros/noetic/setup.bash
source ~/FMCW_LiDAR/Ros_project/devel/setup.bash
rosrun fmcw_lidar_rviz udp_test_sender_node \
  _ip:=127.0.0.1 _port:=9000 _rate:=10.0
```

The synthetic frame also uses `X=forward`, `Y=left`, `Z=up`.

Check the ROS output:

```bash
rostopic hz /fmcw/points
rostopic echo -n 1 /fmcw/points/header
rostopic echo -n 1 /fmcw/points/fields
```

## Main launch arguments

| Argument | Default | Meaning |
|---|---:|---|
| `bind_address` | `0.0.0.0` | Local IPv4 interface to bind |
| `port` | `9000` | UDP receive port |
| `sender_ip` | empty | Optional accepted sender IPv4 address |
| `topic` | `/fmcw/points` | Published PointCloud2 topic |
| `frame_id` | `fmcw_lidar` | PointCloud2 coordinate frame |
| `assembly_timeout` | `0.5` | Incomplete frame timeout in seconds |
| `max_inflight_frames` | `8` | Maximum simultaneous frame assemblies |
| `max_segments_per_frame` | `4096` | Segment-count memory bound |
| `max_frame_points` | `2000000` | Point-count memory bound |
| `socket_receive_buffer` | `4194304` | Requested Linux UDP receive buffer |
| `rviz` | `true` | Start RViz with the supplied config |

## Implementation notes

- The receiver validates the 40-byte header, `FMCW` magic, version, stride,
  segment indices, and exact datagram size.
- Out-of-order segments are restored by index. Duplicates are ignored and
  missing frames expire without publishing a partial cloud.
- XYZIV payload bytes are moved directly into `sensor_msgs/PointCloud2`; there
  is no per-point Python conversion or repacking in the normal receive path.
- The sender timestamp is based on C++ `steady_clock` and has no ROS epoch.
  PointCloud2 therefore uses `ros::Time::now()` at publish time for valid TF/RViz
  synchronization.
- The RViz Fixed Frame and cloud frame default to `fmcw_lidar`, so visualization
  works without TF. When integrating with a robot, set `frame_id` to the real
  sensor frame and provide its static or dynamic transform.
