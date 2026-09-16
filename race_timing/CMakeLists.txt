cmake_minimum_required(VERSION 3.10)
project(race_timing CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(race_timer
    main.cpp
    gpio_io.cpp
    rfid_reader.cpp
    race_manager.cpp
    mqtt_publisher.cpp
)

target_link_libraries(race_timer
    pigpio
    mosquitto
    pthread
    rt
)
