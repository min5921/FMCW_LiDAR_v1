# FMCW LiDAR ROS Noetic 실행 안내서

이 문서는 `Ros_project_Noetic_20260809.zip`을 ROS 컴퓨터로 가져간 뒤
빌드, 자체 시험, 실제 FMCW UDP 연결, RViz 확인까지 수행하는 순서입니다.

## 1. 대상 환경

- Ubuntu 20.04
- ROS1 Noetic
- 유선 LAN 권장
- 기본 UDP 수신 포트: `9000`
- UDP point protocol: `v2`
- ROS 좌표계: `X=전방`, `Y=좌측`, `Z=위쪽`

필요한 패키지가 없다면 설치합니다.

```bash
sudo apt update
sudo apt install build-essential ros-noetic-ros-base ros-noetic-rviz python3-rosdep unzip
```

ROS Noetic 설치와 `rosdep init`가 이미 완료된 컴퓨터라면 다시 수행할 필요가
없습니다.

## 2. ZIP 복사 및 압축 해제

USB나 네트워크를 이용해 `Ros_project_Noetic_20260809.zip`을 ROS 컴퓨터로
복사합니다. 홈 디렉터리에서 작업하는 예시는 다음과 같습니다.

```bash
cd ~
unzip Ros_project_Noetic_20260809.zip
cd ~/Ros_project
ls
```

예상 항목:

```text
README.md
ROS_NOETIC_DEPLOY_KO.md
src
```

## 3. ROS 환경 및 의존성 준비

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
rosdep install --from-paths src --ignore-src -r -y
```

## 4. Release 빌드

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

빌드가 성공하면 다음 실행 파일이 생성됩니다.

```text
devel/lib/fmcw_lidar_rviz/udp_pointcloud_receiver_node
devel/lib/fmcw_lidar_rviz/udp_test_sender_node
```

새 터미널을 열 때마다 다음 두 줄을 다시 실행해야 합니다.

```bash
source /opt/ros/noetic/setup.bash
source ~/Ros_project/devel/setup.bash
```

## 5. 프로토콜 단위 테스트

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
source devel/setup.bash
catkin_make run_tests_fmcw_lidar_rviz
catkin_test_results build/test_results
```

`0 failures`가 출력되어야 합니다.

## 6. 실제 장비 없이 로컬 시험

첫 번째 터미널에서 C++ UDP 수신기와 RViz를 실행합니다.

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
source devel/setup.bash
roslaunch fmcw_lidar_rviz udp_rviz.launch
```

두 번째 터미널에서 C++ 시험 송신기를 실행합니다.

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
source devel/setup.bash
rosrun fmcw_lidar_rviz udp_test_sender_node \
  _ip:=127.0.0.1 _port:=9000 _rate:=10.0
```

RViz에서 움직이는 합성 point cloud가 보이면 ROS 빌드, UDP 수신, frame 조립,
PointCloud2 발행 및 RViz 설정이 모두 정상입니다.

```bash
rostopic hz /fmcw/points
rostopic echo -n 1 /fmcw/points/header
rostopic echo -n 1 /fmcw/points/fields
```

시험을 종료할 때는 각 터미널에서 `Ctrl+C`를 누릅니다.

## 7. 실제 FMCW 송신기 연결

ROS 컴퓨터의 유선 LAN IPv4 주소를 확인합니다.

```bash
ip -4 addr
```

예를 들어 ROS 컴퓨터가 `192.168.0.10`, FMCW 송신기가 `192.168.0.20`이라면
FMCW 프로젝트의 UDP 설정을 다음과 같이 맞춥니다.

```yaml
udp:
  enabled: true
  target_ip: "192.168.0.10"
  target_port: 9000
  packet_format_version: 2
```

ROS 컴퓨터에서 해당 송신기만 허용하여 실행하려면:

```bash
cd ~/Ros_project
source /opt/ros/noetic/setup.bash
source devel/setup.bash
roslaunch fmcw_lidar_rviz udp_rviz.launch \
  sender_ip:=192.168.0.20 port:=9000
```

송신기 IP를 아직 확정하지 않았다면 `sender_ip`를 생략합니다.

```bash
roslaunch fmcw_lidar_rviz udp_rviz.launch port:=9000
```

Ubuntu 방화벽이 활성화되어 있다면 포트를 허용합니다.

```bash
sudo ufw allow 9000/udp
sudo ufw status
```

## 8. RViz 표시 기준

- Fixed Frame: `fmcw_lidar`
- Topic: `/fmcw/points`
- Point fields: `x`, `y`, `z`, `intensity`, `velocity`
- 좌표: `X=전방`, `Y=좌측`, `Z=위쪽`
- 기본 색상: `intensity`

속도로 색칠하려면 RViz의 `FMCW Point Cloud` 항목에서 `Channel Name`을
`velocity`로 변경합니다.

## 9. GUI 없이 수신기만 실행

모니터가 없는 ROS 컴퓨터에서는 RViz를 끌 수 있습니다.

```bash
roslaunch fmcw_lidar_rviz udp_rviz.launch rviz:=false
```

## 10. UDP가 보이지 않을 때

수신 포트와 실제 패킷 유입을 확인합니다.

```bash
ss -lunp | grep 9000
sudo tcpdump -ni any udp port 9000
```

점검 순서:

1. FMCW 송신기의 `udp.enabled`가 `true`인지 확인합니다.
2. `target_ip`가 ROS 컴퓨터의 실제 유선 LAN 주소인지 확인합니다.
3. 양쪽 포트가 모두 `9000`인지 확인합니다.
4. `packet_format_version`이 양쪽 모두 `2`인지 확인합니다.
5. 두 컴퓨터가 같은 subnet에 있고 서로 `ping`되는지 확인합니다.
6. `sender_ip` 필터가 실제 FMCW 송신기 주소와 같은지 확인합니다.
7. 방화벽과 스위치/VLAN 설정을 확인합니다.
8. 수신기 로그의 `packets`, `invalid`, `expired` 통계를 확인합니다.

`packets=0`이면 네트워크 또는 송신 설정 문제입니다. `invalid`가 증가하면
protocol version 또는 payload 형식을 확인합니다. `expired`가 증가하면 raster
frame의 일부 UDP segment가 유실되고 있는 상태입니다.

## 11. 깨끗하게 다시 빌드

다른 컴퓨터의 `build`나 `devel`을 복사했거나 CMake cache 문제가 발생한
경우에만 아래 명령을 사용합니다.

```bash
cd ~/Ros_project
rm -rf build devel
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release
source devel/setup.bash
```

삭제 대상이 반드시 `~/Ros_project/build`와 `~/Ros_project/devel`인지 확인한 뒤
실행하십시오.

## 12. 정상 운용 확인 명령

```bash
rostopic list | grep fmcw
rostopic type /fmcw/points
rostopic hz /fmcw/points
rosnode info /udp_pointcloud_receiver
```

정상 상태에서는 `/fmcw/points`의 type이 `sensor_msgs/PointCloud2`이고 송신
frame rate에 맞춰 topic 주기가 출력됩니다.
