cmake_minimum_required(VERSION 3.0.2)
project(fmcw_lidar_rviz)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  sensor_msgs
  std_msgs
)

add_compile_options(-Wall -Wextra -Wpedantic)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES fmcw_udp_protocol
  CATKIN_DEPENDS roscpp sensor_msgs std_msgs
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

add_library(fmcw_udp_protocol
  src/udp_point_protocol.cpp
)
target_compile_features(fmcw_udp_protocol PUBLIC cxx_std_14)

add_executable(udp_pointcloud_receiver_node
  src/udp_pointcloud_receiver_node.cpp
)
target_compile_features(udp_pointcloud_receiver_node PUBLIC cxx_std_14)
target_link_libraries(udp_pointcloud_receiver_node
  fmcw_udp_protocol
  ${catkin_LIBRARIES}
)

add_executable(udp_test_sender_node
  src/udp_test_sender_node.cpp
)
target_compile_features(udp_test_sender_node PUBLIC cxx_std_14)
target_link_libraries(udp_test_sender_node
  fmcw_udp_protocol
  ${catkin_LIBRARIES}
)

if(CATKIN_ENABLE_TESTING)
  catkin_add_gtest(udp_point_protocol_test
    test/udp_point_protocol_test.cpp
  )
  if(TARGET udp_point_protocol_test)
    target_compile_features(udp_point_protocol_test PUBLIC cxx_std_14)
    target_link_libraries(udp_point_protocol_test
      fmcw_udp_protocol
      ${catkin_LIBRARIES}
    )
  endif()
endif()

install(TARGETS
  fmcw_udp_protocol
  udp_pointcloud_receiver_node
  udp_test_sender_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
)

install(DIRECTORY launch rviz
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)
